/* Regression tests for the shared BabyPOSIX guest descriptor table. */
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>

#define TEST_FD_CAPACITY 253 /* 256 table slots minus stdin/stdout/stderr */

int main(void)
{
    const char payload[] = "fd table";
    char buffer[16];
    int fd, fd2, i;
    int held[TEST_FD_CAPACITY];

    fd = open("babyposix-fd-table.tmp", O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 3) { puts("FAIL: open did not allocate a non-standard descriptor"); return 1; }
    if (read(fd, buffer, 1) != -1) {
        close(fd); unlink("babyposix-fd-table.tmp");
        puts("FAIL: read accepted a write-only descriptor"); return 2;
    }
    if (write(fd, (void *)payload, sizeof(payload) - 1) != (long)(sizeof(payload) - 1)) {
        close(fd); unlink("babyposix-fd-table.tmp"); puts("FAIL: write failed"); return 3;
    }
    if (close(fd) != 0) { unlink("babyposix-fd-table.tmp"); puts("FAIL: close failed"); return 4; }
    if (close(fd) != -1) { unlink("babyposix-fd-table.tmp"); puts("FAIL: double close accepted"); return 5; }

    fd2 = open("babyposix-fd-table.tmp", O_RDONLY);
    if (fd2 != fd) { if (fd2 >= 0) close(fd2); unlink("babyposix-fd-table.tmp"); puts("FAIL: descriptor slot not reused"); return 6; }
    if (read(fd2, buffer, sizeof(buffer)) != (long)(sizeof(payload) - 1)) {
        close(fd2); unlink("babyposix-fd-table.tmp"); puts("FAIL: read failed"); return 7;
    }
    if (close(fd2) != 0) { unlink("babyposix-fd-table.tmp"); puts("FAIL: second close failed"); return 8; }
    if (write(255, (void *)payload, 1) != -1) { unlink("babyposix-fd-table.tmp"); puts("FAIL: invalid descriptor accepted"); return 9; }

    /* Unknown flags must fail without consuming a table slot. */
    if (open("babyposix-fd-table.tmp", O_RDONLY | 0x4000) != -1) {
        puts("FAIL: unknown open flag accepted"); unlink("babyposix-fd-table.tmp"); return 10;
    }

    /* Fill every non-standard slot, then prove exhaustion and recovery. */
    for (i = 0; i < TEST_FD_CAPACITY; ++i) {
        held[i] = open("babyposix-fd-table.tmp", O_RDONLY);
        if (held[i] < 3) {
            while (i > 0) close(held[--i]);
            unlink("babyposix-fd-table.tmp");
            puts("FAIL: descriptor table filled before its documented capacity"); return 11;
        }
    }
    if (open("babyposix-fd-table.tmp", O_RDONLY) != -1) {
        for (i = 0; i < TEST_FD_CAPACITY; ++i) close(held[i]);
        unlink("babyposix-fd-table.tmp");
        puts("FAIL: open succeeded when descriptor table was full"); return 12;
    }
    if (close(held[TEST_FD_CAPACITY - 1]) != 0) {
        for (i = 0; i < TEST_FD_CAPACITY - 1; ++i) close(held[i]);
        unlink("babyposix-fd-table.tmp");
        puts("FAIL: close failed after descriptor exhaustion"); return 13;
    }
    fd = open("babyposix-fd-table.tmp", O_RDONLY);
    if (fd != held[TEST_FD_CAPACITY - 1]) {
        if (fd >= 0) close(fd);
        for (i = 0; i < TEST_FD_CAPACITY - 1; ++i) close(held[i]);
        unlink("babyposix-fd-table.tmp");
        puts("FAIL: table did not recover after freeing a slot"); return 14;
    }
    if (close(fd) != 0) { puts("FAIL: close after recovery failed"); return 15; }
    for (i = 0; i < TEST_FD_CAPACITY - 1; ++i) close(held[i]);

    if (unlink("babyposix-fd-table.tmp") != 0) { puts("FAIL: unlink failed"); return 16; }
    if (write(STDOUT_FILENO, (void *)"BabyPOSIX fd table: ok\n", 23) != 23) return 17;
    return 0;
}
