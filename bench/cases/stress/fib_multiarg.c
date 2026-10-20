/* Non-scorciabile: ricorsione multi-arg (anti mono 1-arg fast-path). */
#include <stdio.h>
long long fib(int n, long long a, long long b) {
    if (n == 0) return a;
    if (n == 1) return b;
    return fib(n-1, b, a+b);
}
int main() {
    printf("%lld\n", fib(30, 0, 1));
    return 0;
}
