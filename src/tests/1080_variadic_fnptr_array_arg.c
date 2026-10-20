#include <stdio.h>
#include <stdarg.h>
static int add(int a, int b) { return a + b; }
static int mul(int a, int b) { return a * b; }
static int apply(int n, ...)
{
    va_list ap;
    int i, acc = n;
    va_start(ap, n);
    for (i = 0; i < 2; i++) {
        int (*f)(int, int) = va_arg(ap, int (*)(int, int));
        acc = f(acc, 3);
    }
    va_end(ap);
    return acc;
}
static int pick(int (*f)(int, int), int x) { return f(x, x); }
int main(void)
{
    int (*ops[2])(int, int);
    ops[0] = add;
    ops[1] = mul;
    printf("%d\n", apply(4, ops[0], ops[1]));
    printf("%d\n", apply(2, ops[1], add));
    printf("%d %d\n", pick(ops[0], 5), pick(ops[1], 5));
    return 0;
}
