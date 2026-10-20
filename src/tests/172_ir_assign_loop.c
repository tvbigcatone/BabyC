/* Body must run via IrStmt assign path when simple */
#include <stdio.h>
int main()
{
    int i, s;
    s = 0;
    for (i = 0; i < 10; i = i + 1)
        s = s + i;
    printf("%d\n", s);
    return 0;
}
