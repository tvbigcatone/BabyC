/* Repeated descriptor lifecycle regression: open/dup/dup2/close/pipe/reuse. */
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>

#define ROUNDS 300

int main(void)
{
    const char *path = "babyposix-fd-stress.tmp";
    char buf[8];
    int i, fd, copy, pair[2], rc;

    for (i = 0; i < ROUNDS; ++i) {
        fd = open(path, O_RDWR | O_CREAT | O_TRUNC, 0666);
        if (fd < 3) { puts("FAIL: stress open"); return 1; }
        if (write(fd, (void *)"abcdef", 6) != 6 || lseek(fd, 0, SEEK_SET) != 0) {
            close(fd); unlink(path); puts("FAIL: stress seed file"); return 2;
        }
        copy = dup(fd);
        if (copy < 3 || copy == fd) {
            close(fd); unlink(path); puts("FAIL: stress dup"); return 3;
        }
        if (read(copy, buf, 2) != 2 || memcmp(buf, "ab", 2) != 0 ||
            lseek(fd, 0, SEEK_CUR) != 2) {
            close(copy); close(fd); unlink(path); puts("FAIL: stress shared offset"); return 4;
        }

        /* Replace the same target repeatedly; each replacement must close the
         * old target without leaking it, and dup2 must clear FD_CLOEXEC. */
        if (dup2(fd, 40) != 40 || fcntl(40, F_SETFD, FD_CLOEXEC) != 0 ||
            dup2(copy, 40) != 40 || fcntl(40, F_GETFD) != 0) {
            close(40); close(copy); close(fd); unlink(path);
            puts("FAIL: stress dup2 replacement/flags"); return 5;
        }
        if (close(40) != 0 || close(copy) != 0 || close(fd) != 0) {
            unlink(path); puts("FAIL: stress close file descriptors"); return 6;
        }
        if (close(fd) != -1) {
            unlink(path); puts("FAIL: stress stale descriptor accepted"); return 7;
        }

        if (pipe(pair) != 0) { unlink(path); puts("FAIL: stress pipe"); return 8; }
        if (write(pair[1], (void *)"xyz", 3) != 3 || close(pair[1]) != 0) {
            close(pair[0]); unlink(path); puts("FAIL: stress pipe write/close"); return 9;
        }
        rc = (int)read(pair[0], buf, sizeof(buf));
        if (rc != 3 || memcmp(buf, "xyz", 3) != 0 || read(pair[0], buf, 1) != 0) {
            close(pair[0]); unlink(path); puts("FAIL: stress pipe read/EOF"); return 10;
        }
        if (close(pair[0]) != 0) { unlink(path); puts("FAIL: stress pipe close"); return 11; }
    }

    if (unlink(path) != 0) { puts("FAIL: stress cleanup"); return 12; }
    puts("BabyPOSIX fd stress: ok (300 rounds)");
    return 0;
}
