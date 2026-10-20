#include <stdio.h>
int main()
{
    int a[10];
    int *p;
    int *q;
    p = &a[0];
    q = &a[7];
    printf("%d\n", (int)(q - p));
    return 0;
}
