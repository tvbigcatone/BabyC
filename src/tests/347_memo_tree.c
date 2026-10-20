/* Anti-regression: pure recursion memo (LRU+decay) — exponential without cache */
#include <stdio.h>
int tree(int n) {
    if (n <= 0) return 1;
    return tree(n - 1) + tree(n - 1) + n;
}
int main() {
    printf("%d\n", tree(20));
    return 0;
}
