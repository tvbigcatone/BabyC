/* Anti-regression: shallow mutual-ish recursion / multi-call tree */
#include <stdio.h>
int g(int n);
int f(int n) {
    if (n <= 0) return 0;
    return 1 + g(n - 1);
}
int g(int n) {
    if (n <= 0) return 0;
    return 1 + f(n - 1);
}
int main() {
    printf("%d\n", f(20));
    return 0;
}
