#include <stdio.h>
int main()
{
    int i;
    long t;
    char buf[64];
    t = 0;
    for (i = 0; i < 50000; i++) {
        /* format a few ints — exercises FormatPure / Intrinsic path */
        sprintf(buf, "%d %d %d", i, i + 1, i * 2);
        t += buf[0];
    }
    printf("%ld\n", t);
    return 0;
}
