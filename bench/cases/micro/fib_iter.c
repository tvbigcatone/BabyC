#include <stdio.h>
long long fib(int n) {
    long long a = 0, b = 1, t;
    int i;
    if (n <= 0) return 0;
    for (i = 1; i < n; i++) { t = a + b; a = b; b = t; }
    return b;
}
int main() {
    int i;
    long long s = 0;
    for (i = 0; i < 5000; i++) s += fib(30);
    printf("%lld\n", s);
    return 0;
}
