/* Minimal POSIX-style echo smoke test for BabyPOSIX file descriptors. */
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    const char msg[] = "BabyPOSIX echo: ok\n";
    int fd = open("babyposix-echo-test.tmp", O_WRONLY | O_CREAT | O_TRUNC, 0666);
    char buf[64];
    int rd;
    if (fd < 0) { puts("open failed"); return 1; }
    if (write(fd, (void *)msg, strlen(msg)) != (long)strlen(msg)) {
        close(fd); unlink("babyposix-echo-test.tmp"); puts("write failed"); return 2;
    }
    if (close(fd) != 0) { unlink("babyposix-echo-test.tmp"); puts("close failed"); return 3; }
    fd = open("babyposix-echo-test.tmp", O_RDONLY);
    if (fd < 0) { unlink("babyposix-echo-test.tmp"); puts("reopen failed"); return 4; }
    rd = (int)read(fd, buf, sizeof(buf) - 1);
    if (rd < 0) { close(fd); unlink("babyposix-echo-test.tmp"); puts("read failed"); return 5; }
    buf[rd] = '\0';
    close(fd);
    unlink("babyposix-echo-test.tmp");
    if (strcmp(buf, msg) != 0) { puts("content mismatch"); return 6; }
    { const char out[] = "BabyPOSIX fd echo: ok\n";
      if (write(STDOUT_FILENO, (void *)out, sizeof(out) - 1) != (long)(sizeof(out) - 1)) return 7; }
    return 0;
}
