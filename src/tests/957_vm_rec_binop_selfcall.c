/* Anti-regression: integer self-call BinOp must NOT pointer-scale.
 * Root: Call TF_PTR false positive → fib(n-1)+fib(n-2) lowered as *4+add. */
#include <stdio.h>
int calls;
int fib(int n) {
    calls++;
    if (n < 2) return n;
    return fib(n - 1) + fib(n - 2);
}
int tri(int n) {
    if (n < 3) return n;
    return tri(n - 1) + tri(n - 2) + tri(n - 3) % 7;
}
int main(void) {
    int i;
    for (i = 0; i <= 10; i++) {
        calls = 0;
        printf("%d %d %d\n", i, fib(i), calls);
    }
    printf("tri22 %d\n", tri(22));
    return 0;
}
