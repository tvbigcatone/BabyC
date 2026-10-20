#include <stdio.h>
int fib(int n) {
    if (n < 2) return n;
    return fib(n-1) + fib(n-2);
}
int main() {
    printf("%d %d\n", fib(10), fib(15));
    return 0;
}
