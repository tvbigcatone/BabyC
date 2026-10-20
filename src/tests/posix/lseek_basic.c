/* Regression tests for lseek() routed through the BabyPOSIX descriptor table. */
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    const char payload[] = "0123456789";
    char buf[8];
    int fd;
    int pos;

    fd = open("babyposix-lseek.tmp", O_RDWR | O_CREAT | O_TRUNC, 0666);
    if (fd < 3) { puts("FAIL: open"); return 1; }
    if (write(fd, (void *)payload, 10) != 10) {
        close(fd); unlink("babyposix-lseek.tmp"); puts("FAIL: write"); return 2;
    }
    pos = lseek(fd, 0, SEEK_SET);
    if (pos != 0) { close(fd); unlink("babyposix-lseek.tmp"); puts("FAIL: SEEK_SET"); return 3; }
    if (read(fd, buf, 3) != 3 || memcmp(buf, "012", 3) != 0) {
        close(fd); unlink("babyposix-lseek.tmp"); puts("FAIL: read after SEEK_SET"); return 4;
    }
    pos = lseek(fd, 2, SEEK_CUR);
    if (pos != 5) { close(fd); unlink("babyposix-lseek.tmp"); puts("FAIL: SEEK_CUR"); return 5; }
    pos = lseek(fd, -2, SEEK_END);
    if (pos != 8) { close(fd); unlink("babyposix-lseek.tmp"); puts("FAIL: SEEK_END"); return 6; }
    if (read(fd, buf, 2) != 2 || memcmp(buf, "89", 2) != 0) {
        close(fd); unlink("babyposix-lseek.tmp"); puts("FAIL: read after SEEK_END"); return 7;
    }
    if (lseek(fd, 0, 99) != -1) {
        close(fd); unlink("babyposix-lseek.tmp"); puts("FAIL: invalid whence accepted"); return 8;
    }
    if (close(fd) != 0) { unlink("babyposix-lseek.tmp"); puts("FAIL: close"); return 9; }
    if (lseek(fd, 0, SEEK_SET) != -1) { unlink("babyposix-lseek.tmp"); puts("FAIL: closed descriptor accepted"); return 10; }
    if (unlink("babyposix-lseek.tmp") != 0) { puts("FAIL: unlink"); return 11; }
    puts("BabyPOSIX lseek: ok");
    return 0;
}
