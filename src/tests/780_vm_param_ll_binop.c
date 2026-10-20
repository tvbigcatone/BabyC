#include <stdio.h>
long long f(int n, int k) {
    long long b = 1;
    int i;
    for (i = 1; i < n; i++) b = b + (k & 1);
    return b;
}
int main() { printf("%lld\n", f(10, 7)); return 0; }
