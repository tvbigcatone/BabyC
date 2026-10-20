#include <stdio.h>
/* Anti-regression: call-arg commas vs comma operator in (a,b,c) */
int main()
{
    int a = 1, b = 2, c = 3;
    printf("%d %d %d\n", a, b, c);
    printf("%d\n", (a, b, c));
    return 0;
}
