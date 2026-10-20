#include <stdio.h>
/* Body IR + int params via ParamArray frame */
int add3(int a, int b, int c)
{
    return a + b + c;
}
void main()
{
    int i, s;
    s = 0;
    for (i = 0; i < 10; i++)
        s = s + add3(i, 1, 2);
    printf("s %d\n", s);
}
