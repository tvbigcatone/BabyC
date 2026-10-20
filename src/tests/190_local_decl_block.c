#include <stdio.h>
int f(void)
{
    int a;
    a = 1;
    {
        int b;
        b = 2;
        a = a + b;
    }
    return a;
}
int main()
{
    printf("%d\n", f());
    return 0;
}
