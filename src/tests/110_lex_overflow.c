/* Costanti che superano il range di long ma non di long long.
 * Bug #5: LexGetNumber tronca silenziosamente a long. */

#include <stdio.h>

void main()
{
    long long big = 1099511627776LL;
    printf("%lld\n", big);

    long long big2 = 4294967296LL;
    printf("%lld\n", big2);

    unsigned long long h = 0xFFFFFFFFFFFFFFFFULL;
    printf("%llu\n", h);
}