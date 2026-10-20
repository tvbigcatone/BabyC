/* Regression tests for descriptor-slot reuse and per-descriptor flags. */
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>

int main(void)
{
    const char *path = "babyposix-fd-reuse.tmp";
    int fd, copy, reopened, reused;

    fd = open(path, O_RDWR | O_CREAT | O_TRUNC, 0666);
    if (fd < 3) {
        puts("FAIL: initial open");
        return 1;
    }
    if (fcntl(fd, F_SETFD, FD_CLOEXEC) != 0 ||
        fcntl(fd, F_GETFD) != FD_CLOEXEC) {
        close(fd); unlink(path);
        puts("FAIL: set source descriptor flag");
        return 2;
    }

    copy = dup(fd);
    if (copy < 3 || copy == fd) {
        close(fd); unlink(path);
        puts("FAIL: dup allocation");
        return 3;
    }
    if (fcntl(copy, F_GETFD) != 0) {
        close(copy); close(fd); unlink(path);
        puts("FAIL: dup inherited FD_CLOEXEC");
        return 4;
    }

    if (close(fd) != 0) {
        close(copy); unlink(path);
        puts("FAIL: close before open reuse");
        return 5;
    }
    reopened = open(path, O_RDONLY);
    if (reopened != fd) {
        if (reopened >= 0) close(reopened);
        close(copy); unlink(path);
        puts("FAIL: open did not reuse lowest free slot");
        return 6;
    }
    if (fcntl(reopened, F_GETFD) != 0) {
        close(reopened); close(copy); unlink(path);
        puts("FAIL: reused open slot retained FD_CLOEXEC");
        return 7;
    }

    if (dup2(reopened, 40) != 40 ||
        fcntl(40, F_SETFD, FD_CLOEXEC) != 0 ||
        close(40) != 0) {
        close(reopened); close(copy); unlink(path);
        puts("FAIL: prepare and release target slot");
        return 8;
    }
    if (close(copy) != 0) {
        close(reopened); unlink(path);
        puts("FAIL: close duplicate before slot-reuse check");
        return 9;
    }
    copy = -1;
    reused = dup(reopened);
    if (reused != fd + 1 || fcntl(reused, F_GETFD) != 0) {
        if (reused >= 3) close(reused);
        close(reopened); unlink(path);
        puts("FAIL: dup did not reuse lowest free slot with clear flags");
        return 10;
    }

    if (close(reused) != 0 || close(reopened) != 0) {
        unlink(path);
        puts("FAIL: final close");
        return 11;
    }
    if (close(40) != -1) {
        unlink(path);
        puts("FAIL: closed target descriptor remained valid");
        return 12;
    }
    if (unlink(path) != 0) {
        puts("FAIL: cleanup");
        return 13;
    }
    puts("BabyPOSIX fd reuse: ok");
    return 0;
}
