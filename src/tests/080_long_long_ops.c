#include <stdio.h>

int main()
{
    long long a = 1000000000LL;
    long long b = a * 1000LL;
    long long c = -9223372036854775807LL;
    unsigned long long mask = 0xDEADBEEFCAFEBABEULL;

    printf("a        = %lld\n", a);
    printf("b (1e12) = %lld\n", b);
    printf("c (min)  = %lld\n", c);
    printf("c hex    = 0x%016llX\n", (unsigned long long)c);
    printf("mask     = 0x%016llX\n", mask);
    printf("mask>>32 = 0x%08llX\n", mask >> 32);
    printf("mask&lo  = 0x%08llX\n", mask & 0xFFFFFFFFULL);
    return 0;
}
