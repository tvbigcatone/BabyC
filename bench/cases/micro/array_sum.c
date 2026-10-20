#include <stdio.h>
#define N 10000
int main() {
    int a[N];
    int i, k;
    long long s = 0;
    for (i = 0; i < N; i++) a[i] = i;
    for (k = 0; k < 200; k++)
        for (i = 0; i < N; i++) s += a[i];
    printf("%lld\n", s);
    return 0;
}
