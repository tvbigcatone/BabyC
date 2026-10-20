/* Anti-regression: IR ternary else-branch must not consume call-arg commas.
 * Root cause of 424_ptr_relops and 336_cross_ir_token_ptr. */
#include <stdio.h>

int add2(int a, int b) { return a + b; }
int sel(int c, const char *s, int *n) {
    if (c) { *n = *n + 1; return (int)s[0]; }
    return 0;
}

int main() {
    int x = 0, y = 1;
    int *p = &x;
    int *q = &y;
    int n = 0;
    /* three ternaries as successive printf args */
    printf("%d %d %d\n", p < q ? 1 : 0, p == q ? 1 : 0, p > q ? 1 : 0);
    /* ternary then comma-separated call args */
    printf("%d\n", add2(x ? 10 : 3, y ? 4 : 99));
    /* ternary producing string literal + pointer arg (323 shape) */
    printf("%d %d\n", sel(1, n ? "A" : "B", &n), n);
    /* nested: true/false branches */
    printf("%d %d\n", 0 ? 1 : 2, 1 ? 3 : 4);
    return 0;
}
