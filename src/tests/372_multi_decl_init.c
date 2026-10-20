#include <stdio.h>
/* Anti-regression: int a=1, b=2, c=3 declaration list */
int main()
{
    int a = 10, b = 20, c = 30;
    printf("%d %d %d\n", a, b, c);
    return 0;
}
