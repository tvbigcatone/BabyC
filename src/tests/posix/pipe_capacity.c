/* Verify pipe() rolls back both host endpoints if only one guest slot remains. */
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>

#define TEST_FD_CAPACITY 253

int main(void)
{
    int held[TEST_FD_CAPACITY];
    int pair[2];
    int i;

    for (i = 0; i < TEST_FD_CAPACITY - 1; ++i) {
        held[i] = open("babyposix-pipe-capacity.tmp", O_RDONLY | O_CREAT, 0666);
        if (held[i] < 3) {
            while (i > 0) close(held[--i]);
            unlink("babyposix-pipe-capacity.tmp");
            puts("FAIL: could not reserve descriptor slots");
            return 1;
        }
    }

    if (pipe(pair) != -1) {
        close(pair[0]); close(pair[1]);
        for (i = 0; i < TEST_FD_CAPACITY - 1; ++i) close(held[i]);
        unlink("babyposix-pipe-capacity.tmp");
        puts("FAIL: pipe succeeded with only one slot free");
        return 2;
    }

    /* A failed pipe must not leak the single available guest slot. */
    if (close(held[TEST_FD_CAPACITY - 2]) != 0) {
        for (i = 0; i < TEST_FD_CAPACITY - 1; ++i)
            if (i != TEST_FD_CAPACITY - 2) close(held[i]);
        unlink("babyposix-pipe-capacity.tmp");
        puts("FAIL: could not free a descriptor slot");
        return 3;
    }
    held[TEST_FD_CAPACITY - 2] = -1;
    if (pipe(pair) != 0) {
        for (i = 0; i < TEST_FD_CAPACITY - 1; ++i)
            if (i != TEST_FD_CAPACITY - 2) close(held[i]);
        unlink("babyposix-pipe-capacity.tmp");
        puts("FAIL: pipe did not recover after freeing two slots");
        return 4;
    }
    if (close(pair[0]) != 0 || close(pair[1]) != 0) {
        for (i = 0; i < TEST_FD_CAPACITY - 1; ++i)
            if (i != TEST_FD_CAPACITY - 2) close(held[i]);
        unlink("babyposix-pipe-capacity.tmp");
        puts("FAIL: close recovered pipe");
        return 5;
    }
    for (i = 0; i < TEST_FD_CAPACITY - 1; ++i)
        if (i != TEST_FD_CAPACITY - 2) close(held[i]);
    unlink("babyposix-pipe-capacity.tmp");
    puts("BabyPOSIX pipe capacity: ok");
    return 0;
}
