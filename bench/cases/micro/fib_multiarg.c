#include <stdio.h>
/* Dual recursion with two integer args — not a thin 30-call wrapper. */
int fib2(int a, int b)
{
    if (a < 2)
        return a + b;
    return fib2(a - 1, b) + fib2(a - 2, b + 1);
}
int main(void)
{
    printf("%d\n", fib2(20, 0));
    return 0;
}
