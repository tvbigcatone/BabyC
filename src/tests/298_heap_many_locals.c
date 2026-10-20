/* Anti-regression: many short-lived locals (IR/Value alloc churn) */
#include <stdio.h>
int main()
{
    int i, sum = 0;
    for (i = 0; i < 3000; i++) {
        int a = i;
        int b = i * 2;
        int c = a + b;
        int d = c - 1;
        sum += a + b + c + d;
    }
    printf("%d\n", sum);
    return 0;
}
