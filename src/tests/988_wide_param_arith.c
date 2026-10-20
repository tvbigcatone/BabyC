#include <stdio.h>
/* 64-bit parameters in arithmetic: the parameter's width must reach the
 * operation (linear lowered `big % 1000` on a long long param as 32-bit). */
long long f1(long long big) { big += 1; return big % 1000; }
long long f2(long long a, long long b) { return (a * b) / 7 + (a >> 33); }
unsigned long long f3(unsigned long long u) { return (u >> 40) ^ (u % 100000007ULL); }
int f4(long long v, int s) { return (int)((v >> s) & 0xffff); }
long f5(long x) { return x / 3 - x % 3; }
int main(void)
{
    long long acc = 0;
    int i;
    for (i = 0; i < 5; i++)
        acc += f1(123456789012LL + i) + f4(987654321098LL, i * 8);
    printf("%lld %lld\n", acc, f2(3000000000LL, 5));
    printf("%llu %ld\n", f3(18446744073709551000ULL), f5(-10000000000L));
    return 0;
}
