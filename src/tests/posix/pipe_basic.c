/* Regression tests for pipe() through the shared BabyPOSIX descriptor table. */
#include <unistd.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    int fd[2];
    char buf[16];
    long n;

    if (pipe(fd) != 0) {
        puts("FAIL: pipe creation");
        return 1;
    }
    if (fd[0] < 3 || fd[1] < 3 || fd[0] == fd[1]) {
        close(fd[0]); close(fd[1]);
        puts("FAIL: invalid guest descriptors");
        return 2;
    }
    if (write(fd[1], (void *)"pipe-data", 9) != 9) {
        close(fd[0]); close(fd[1]);
        puts("FAIL: pipe write");
        return 3;
    }
    if (close(fd[1]) != 0) {
        close(fd[0]);
        puts("FAIL: close writer");
        return 4;
    }
    n = read(fd[0], buf, sizeof(buf));
    if (n != 9 || memcmp(buf, "pipe-data", 9) != 0) {
        close(fd[0]);
        puts("FAIL: pipe read payload");
        return 5;
    }
    /* Closing the last writer must make the drained pipe return EOF. */
    if (read(fd[0], buf, sizeof(buf)) != 0) {
        close(fd[0]);
        puts("FAIL: EOF after writer close");
        return 6;
    }
    if (write(fd[0], (void *)"x", 1) != -1) {
        close(fd[0]);
        puts("FAIL: write accepted on read endpoint");
        return 7;
    }
    if (read(fd[1], buf, 1) != -1) {
        close(fd[0]);
        puts("FAIL: read accepted on closed writer");
        return 8;
    }
    if (close(fd[0]) != 0) {
        puts("FAIL: close reader");
        return 9;
    }
    if (pipe((int *)0) != -1) {
        puts("FAIL: null output pointer accepted");
        return 10;
    }
    puts("BabyPOSIX pipe: ok");
    return 0;
}
