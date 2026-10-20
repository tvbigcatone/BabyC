/* Error-contract regression tests for BabyPOSIX descriptor APIs. */
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    const char *path = "babyposix-fd-errors.tmp";
    const char payload[] = "error-contract";
    char buf[sizeof(payload)];
    int fd, pair[2], rc;

    /* Invalid open flags must fail without creating a descriptor or file. */
    rc = open(path, 0x40000000, 0666);
    if (rc != -1) {
        if (rc >= 0) close(rc);
        unlink(path);
        puts("FAIL: open accepted unknown flags");
        return 1;
    }

    fd = open(path, O_RDWR | O_CREAT | O_TRUNC, 0666);
    if (fd < 3) { puts("FAIL: seed open"); return 2; }
    if (write(fd, (void *)payload, sizeof(payload)) != (long)sizeof(payload)) {
        close(fd); unlink(path); puts("FAIL: seed write"); return 3;
    }

    if (read(-1, buf, 1) != -1 || write(-1, (void *)"x", 1) != -1) {
        close(fd); unlink(path); puts("FAIL: invalid descriptor accepted by I/O"); return 4;
    }
    if (lseek(fd, 0, 99) != -1) {
        close(fd); unlink(path); puts("FAIL: lseek accepted invalid whence"); return 5;
    }
    if (dup2(fd, -1) != -1 || dup2(fd, 256) != -1) {
        close(fd); unlink(path); puts("FAIL: dup2 accepted out-of-range target"); return 6;
    }
    if (fcntl(fd, F_SETFD, 0x4000) != -1) {
        close(fd); unlink(path); puts("FAIL: fcntl accepted unknown descriptor flags"); return 7;
    }

    /* Failed operations must leave the source descriptor usable. */
    if (lseek(fd, 0, SEEK_SET) != 0 ||
        read(fd, buf, sizeof(payload)) != (long)sizeof(payload) ||
        memcmp(buf, payload, sizeof(payload)) != 0) {
        close(fd); unlink(path); puts("FAIL: source descriptor damaged by failed operation"); return 8;
    }
    if (close(fd) != 0 || close(fd) != -1) {
        unlink(path); puts("FAIL: close/closed-descriptor contract"); return 9;
    }
    if (pipe(pair) != 0) { unlink(path); puts("FAIL: pipe after invalid operations"); return 10; }
    if (close(pair[0]) != 0 || close(pair[1]) != 0) {
        unlink(path); puts("FAIL: pipe endpoint cleanup"); return 11;
    }
    if (unlink(path) != 0) { puts("FAIL: cleanup"); return 12; }
    puts("BabyPOSIX fd errors: ok");
    return 0;
}
