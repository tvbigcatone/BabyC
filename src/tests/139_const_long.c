/* Integer constants: values that need >32 bits use long long.
 * On Windows LLP64, long is 32-bit; long long is 64-bit on both Win and Unix. */

#include <stdio.h>

void main()
{
    int a = 1000000;
    printf("%d\n", a);

    long long b = 4294967296LL;
    printf("%lld\n", b);

    long long c = 4294967296LL;
    printf("%lld\n", c);

    unsigned long long d = 0xFFFFFFFFFFFFFFFFULL;
    printf("%llu\n", d);
}
