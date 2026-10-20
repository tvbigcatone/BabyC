#include <stdio.h>
int main(void)
{
    long long d = -64; int a = -16;
    printf("%lld %d %lld\n", d >> 3, a >> 2, (-7LL) >> 1);
    return 0;
}
