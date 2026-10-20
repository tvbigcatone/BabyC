/* Shadowing must invalidate VariableGet cache */
#include <stdio.h>
int main()
{
    int x;
    x = 1;
    {
        int x;
        x = 2;
        printf("%d\n", x);
    }
    printf("%d\n", x);
    return 0;
}
