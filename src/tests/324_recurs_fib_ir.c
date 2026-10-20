/* Anti-regression: pure recursive int function must use Body IR + fast call path.
 * Correctness of fib(15) and that it completes quickly (no token-path blowup). */
#include <stdio.h>
int fib(int n) {
    if (n <= 1) return n;
    return fib(n - 1) + fib(n - 2);
}
int main() {
    printf("%d\n", fib(15));
    return 0;
}
