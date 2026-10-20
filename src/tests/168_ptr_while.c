#include <stdio.h>
int main()
{
    int a[5];
    int *p;
    int *end;
    int i, s;
    for (i = 0; i < 5; i = i + 1)
        a[i] = i * 10;
    s = 0;
    p = &a[0];
    end = &a[5];
    while (p < end)
    {
        s = s + *p;
        p = p + 1;
    }
    printf("%d\n", s);
    return 0;
}
