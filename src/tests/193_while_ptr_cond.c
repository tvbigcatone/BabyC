#include <stdio.h>
int main()
{
    int a[5];
    int *p;
    int s;
    int i;
    for (i = 0; i < 5; i = i + 1)
        a[i] = i + 1;
    s = 0;
    p = a;
    while (p < a + 5)
    {
        s = s + *p;
        p = p + 1;
    }
    printf("%d\n", s);
    return 0;
}
