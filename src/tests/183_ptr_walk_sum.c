#include <stdio.h>
int main()
{
    int a[4];
    int *p;
    int s;
    a[0]=1; a[1]=2; a[2]=3; a[3]=4;
    s = 0;
    p = a;
    while (p < a + 4)
    {
        s = s + *p;
        p = p + 1;
    }
    printf("%d\n", s);
    return 0;
}
