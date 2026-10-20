/* Block-scope local shadows parameter (valid C) */
#include <stdio.h>
int f(int x)
{
    int r;
    r = x;
    {
        int y;
        y = 7;
        r = r + y;
    }
    return r;
}
int main()
{
    printf("%d\n", f(3));
    return 0;
}
