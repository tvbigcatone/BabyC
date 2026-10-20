#include <stdio.h>
int main()
{
    int a[3];
    int *p;
    a[0]=0; a[1]=0; a[2]=0;
    p = a;
    printf("%d\n", p == a);
    printf("%d\n", p < a + 3);
    p = a + 3;
    printf("%d\n", p == a + 3);
    printf("%d\n", p > a);
    return 0;
}
