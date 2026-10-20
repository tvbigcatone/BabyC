#include <stdio.h>

/* Anti-regression: for-loop with pointer walk; mixed int loop still fast-path safe. */

void main()
{
    char buf[8];
    char *p;
    int i, sum;

    buf[0] = '1'; buf[1] = '2'; buf[2] = '3'; buf[3] = 0;

    p = buf;
    for (; *p != 0; p++)
        printf("%c", *p);
    printf("\n");

    /* integer for must still terminate and sum correctly (Irb / tight path) */
    sum = 0;
    for (i = 0; i < 10; i++)
        sum = sum + i;
    printf("sum %d\n", sum);
    printf("ok\n");
}
