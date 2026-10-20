#include <stdio.h>
int main(void)
{
    long a[4];
    long long b[3];
    int i;
    for (i = 0; i < 4; i++) a[i] = (long)(i + 1) * 1000;
    for (i = 0; i < 3; i++) b[i] = (long long)(i + 1) * 10000000000LL;
    a[1] += 5;
    b[0] = b[2];
    printf("%ld %ld %lld %lld\n", a[0], a[1], b[0], b[1]);
    return 0;
}
