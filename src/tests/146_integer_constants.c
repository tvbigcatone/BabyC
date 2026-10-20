#include <stdio.h>

void main()
{
    /* small decimal → int */
    printf("%d\n", 42);

    /* 2^31 does not fit in signed 32-bit int → long or long long */
    printf("%lld\n", (long long)2147483648);

    /* 2^32 unsuffixed decimal → long long on LLP64 */
    printf("%lld\n", (long long)4294967296);

    /* hex that needs unsigned */
    printf("%u\n", 0xFFFFFFFF);

    /* explicit LL */
    printf("%lld\n", 1LL << 40);
}
