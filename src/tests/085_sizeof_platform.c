#include <stdio.h>

int main()
{
    printf("sizeof_char=%d\n", (int)sizeof(char));
    printf("sizeof_short=%d\n", (int)sizeof(short));
    printf("sizeof_int=%d\n", (int)sizeof(int));
    printf("sizeof_long_long=%d\n", (int)sizeof(long long));
    printf("sizeof_double=%d\n", (int)sizeof(double));

    if (sizeof(void *) == sizeof(char *))
        printf("ptr_consistent=ok\n");
    else
        printf("ptr_consistent=bad\n");

    if (sizeof(long long) == 8)
        printf("long_long_64=ok\n");
    else
        printf("long_long_64=bad\n");

    /* Windows LLP64: long is 4; LP64 Unix: long is 8. Only check consistency. */
    if (sizeof(long) == 4 || sizeof(long) == 8)
        printf("long_size=ok\n");
    else
        printf("long_size=bad\n");

    return 0;
}
