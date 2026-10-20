#include <stdio.h>
/* Anti-regression: comma operator in statement, grouping, for init/inc */
int main()
{
    int a, b, c;
    a = 1, b = 2;
    c = (a, b);
    printf("%d %d %d\n", a, b, c);
    for (a = 0, b = 0; a < 3; a++, b += 2)
    {
        if (a > 0)
            printf(" ");
        printf("%d:%d", a, b);
    }
    printf("\n");
    c = (a = 10, b = 20, a + b);
    printf("%d %d %d\n", a, b, c);
    return 0;
}
