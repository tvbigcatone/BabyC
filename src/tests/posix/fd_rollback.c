/* Failure-path regression tests for descriptor allocation and dup2 atomicity. */
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>

#define FD_CAPACITY 253 /* 256 guest slots minus standard descriptors 0..2 */

int main(void)
{
    const char *path = "babyposix-fd-rollback.tmp";
    const char payload[] = "keep-target";
    char buf[sizeof(payload)];
    int fd, pair[2], held[FD_CAPACITY - 1];
    int i, probe, dup_rc, seek_rc;
    long read_rc;

    fd = open(path, O_RDWR | O_CREAT | O_TRUNC, 0666);
    if (fd < 3) { puts("FAIL: rollback seed open"); return 1; }
    if (write(fd, (void *)payload, sizeof(payload)) != (long)sizeof(payload)) {
        close(fd); unlink(path); puts("FAIL: rollback seed write"); return 2;
    }
    if (dup2(fd, 40) != 40) {
        close(fd); unlink(path); puts("FAIL: rollback initial dup2"); return 3;
    }

    /* A failed dup2 must not close or replace its existing target. Keep
     * each check separate so Windows failures identify the exact operation. */
    dup_rc = dup2(999, 40);
    if (dup_rc != -1) {
        printf("FAIL: dup2(invalid, target) returned %d, expected -1\n", dup_rc);
        close(40); close(fd); unlink(path); return 4;
    }
    seek_rc = lseek(40, 0, SEEK_SET);
    if (seek_rc != 0) {
        printf("FAIL: target lseek after failed dup2 returned %d\n", seek_rc);
        close(40); close(fd); unlink(path); return 4;
    }
    read_rc = read(40, buf, sizeof(payload));
    if (read_rc != (long)sizeof(payload)) {
        printf("FAIL: target read after failed dup2 returned %ld, expected %lu\n",
               read_rc, (unsigned long)sizeof(payload));
        close(40); close(fd); unlink(path); return 4;
    }
    if (memcmp(buf, payload, sizeof(payload)) != 0) {
        puts("FAIL: target bytes changed after failed dup2");
        close(40); close(fd); unlink(path); return 4;
    }
    if (dup2(40, 40) != 40) {
        close(40); close(fd); unlink(path);
        puts("FAIL: dup2(fd, fd) should succeed"); return 5;
    }
    if (close(40) != 0 || close(fd) != 0) {
        unlink(path); puts("FAIL: rollback seed close"); return 6;
    }

    /* Leave exactly one guest slot free. pipe() must roll back its first
     * endpoint if the second endpoint cannot obtain a table slot. */
    for (i = 0; i < FD_CAPACITY - 1; ++i) {
        held[i] = open(path, O_RDONLY);
        if (held[i] < 3) {
            while (i > 0) close(held[--i]);
            unlink(path); puts("FAIL: could not prepare near-full table"); return 7;
        }
    }
    if (pipe(pair) != -1) {
        close(pair[0]); close(pair[1]);
        for (i = 0; i < FD_CAPACITY - 1; ++i) close(held[i]);
        unlink(path); puts("FAIL: pipe succeeded with only one slot free"); return 8;
    }

    /* The rolled-back slot must be available immediately. */
    probe = open(path, O_RDONLY);
    if (probe < 3) {
        for (i = 0; i < FD_CAPACITY - 1; ++i) close(held[i]);
        unlink(path); puts("FAIL: pipe rollback leaked a guest slot"); return 9;
    }
    if (close(probe) != 0) {
        for (i = 0; i < FD_CAPACITY - 1; ++i) close(held[i]);
        unlink(path); puts("FAIL: close after pipe rollback"); return 10;
    }

    /* Free two slots and ensure pipe allocation can recover normally. */
    if (close(held[FD_CAPACITY - 2]) != 0 || close(held[FD_CAPACITY - 3]) != 0) {
        for (i = 0; i < FD_CAPACITY - 3; ++i) close(held[i]);
        unlink(path); puts("FAIL: freeing slots after rollback"); return 11;
    }
    if (pipe(pair) != 0) {
        for (i = 0; i < FD_CAPACITY - 3; ++i) close(held[i]);
        unlink(path); puts("FAIL: pipe did not recover after slots freed"); return 12;
    }
    if (write(pair[1], (void *)"R", 1) != 1 || close(pair[1]) != 0 ||
        read(pair[0], buf, 1) != 1 || buf[0] != 'R' || close(pair[0]) != 0) {
        close(pair[0]); unlink(path);
        puts("FAIL: recovered pipe did not transfer data"); return 13;
    }

    for (i = 0; i < FD_CAPACITY - 3; ++i) close(held[i]);
    if (unlink(path) != 0) { puts("FAIL: rollback cleanup"); return 14; }
    puts("BabyPOSIX fd rollback: ok");
    return 0;
}
