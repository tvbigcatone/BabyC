#include <stdio.h>
/* CROSS: LICM const + array fn in same loop */
int scale_add(int *a, int n, int k)
{
    int i, s;
    s = 0;
    for (i = 0; i < n; i++)
        s = s + a[i] * k;
    return s;
}

int main()
{
    int buf[4];
    int i, k, s;
    k = 0;
    for (i = 0; i < 4; i++)
        buf[i] = i + 1;
    s = 0;
    for (i = 0; i < 3; i++)
    {
        k = 2; /* invariant */
        s = s + scale_add(buf, 4, k);
    }
    printf("%d %d\n", s, k);
    return 0;
}
