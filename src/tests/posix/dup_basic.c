/* Regression tests for dup()/dup2() and shared open-file position. */
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    const char payload[] = "abc";
    char buf[8];
    int fd, copy, target, rc;

    fd = open("babyposix-dup.tmp", O_RDWR | O_CREAT | O_TRUNC, 0666);
    if (fd < 3) { puts("FAIL: open source"); return 1; }
    if (write(fd, (void *)payload, 3) != 3) {
        close(fd); unlink("babyposix-dup.tmp"); puts("FAIL: initial write"); return 2;
    }

    copy = dup(fd);
    if (copy < 3 || copy == fd) {
        close(fd); unlink("babyposix-dup.tmp"); puts("FAIL: dup"); return 3;
    }
    if (write(copy, (void *)"d", 1) != 1 || lseek(fd, 0, SEEK_CUR) != 4) {
        if (copy >= 0) close(copy);
        close(fd); unlink("babyposix-dup.tmp"); puts("FAIL: shared offset after dup"); return 4;
    }
    if (lseek(fd, 0, SEEK_SET) != 0 || read(copy, buf, 2) != 2 || memcmp(buf, "ab", 2) != 0) {
        if (copy >= 0) close(copy);
        close(fd); unlink("babyposix-dup.tmp"); puts("FAIL: shared offset read"); return 5;
    }
    if (close(copy) != 0 || lseek(fd, 0, SEEK_CUR) != 2) {
        close(fd); unlink("babyposix-dup.tmp"); puts("FAIL: closing duplicate affected source"); return 7;
    }
    copy = -1;

    target = open("babyposix-dup-target.tmp", O_RDWR | O_CREAT | O_TRUNC, 0666);
    if (target < 3) {
        close(fd); unlink("babyposix-dup.tmp"); puts("FAIL: open target"); return 8;
    }
    if (write(target, (void *)"old", 3) != 3) {
        close(target); if (copy >= 0) close(copy); close(fd); unlink("babyposix-dup.tmp"); unlink("babyposix-dup-target.tmp"); puts("FAIL: target write"); return 7;
    }

    rc = dup2(fd, target);
    if (rc != target) {
        close(target); if (copy >= 0) close(copy); close(fd); unlink("babyposix-dup.tmp"); unlink("babyposix-dup-target.tmp"); puts("FAIL: dup2 return"); return 8;
    }
    if (lseek(fd, 1, SEEK_SET) != 1 || read(target, buf, 1) != 1 || buf[0] != 'b') {
        close(target); if (copy >= 0) close(copy); close(fd); unlink("babyposix-dup.tmp"); unlink("babyposix-dup-target.tmp"); puts("FAIL: dup2 shared offset"); return 9;
    }
    if (dup2(fd, fd) != fd) {
        close(target); if (copy >= 0) close(copy); close(fd); unlink("babyposix-dup.tmp"); unlink("babyposix-dup-target.tmp"); puts("FAIL: dup2 same descriptor"); return 10;
    }
    if (dup(-1) != -1 || dup2(-1, target) != -1 || dup2(fd, 256) != -1) {
        close(target); if (copy >= 0) close(copy); close(fd); unlink("babyposix-dup.tmp"); unlink("babyposix-dup-target.tmp"); puts("FAIL: invalid descriptor accepted"); return 11;
    }

    if ((copy >= 0 && close(copy) != 0) || close(target) != 0 || close(fd) != 0) {
        unlink("babyposix-dup.tmp"); unlink("babyposix-dup-target.tmp"); puts("FAIL: close"); return 12;
    }
    if (unlink("babyposix-dup.tmp") != 0 || unlink("babyposix-dup-target.tmp") != 0) {
        puts("FAIL: unlink"); return 13;
    }
    puts("BabyPOSIX dup: ok");
    return 0;
}
