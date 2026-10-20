/* Anti-regression: multi-arg recursion + BinOp under VM. */
#include <stdio.h>
int fib2(int a, int b) {
    if (a < 2) return a + b;
    return fib2(a - 1, b) + fib2(a - 2, b + 1);
}
int main(void) {
    printf("%d\n", fib2(20, 0));
    return 0;
}
