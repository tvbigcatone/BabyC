/* Anti-regression: IR recursive call mixed with non-recursive Body IR */
#include <stdio.h>
int add(int a, int b) { return a + b; }
int fib(int n) {
    if (n <= 1) return n;
    return add(fib(n - 1), fib(n - 2));
}
int main() {
    printf("%d\n", fib(12));
    return 0;
}
