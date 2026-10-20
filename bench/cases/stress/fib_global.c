/* Non-scorciabile: ricorsione pura + contatore globale. */
#include <stdio.h>
long long calls;
long long fib(int n) {
    calls++;
    if (n < 2) return n;
    return fib(n-1) + fib(n-2);
}
int main() {
    long long r = fib(30);
    printf("%lld %lld\n", r, calls);
    return 0;
}
