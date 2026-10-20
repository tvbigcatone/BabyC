#include <stdio.h>

int fold_indexed(int *p, int n)
{
    if (n <= 0)
        return 0;
    return p[n - 1] + fold_indexed(p, n - 1);
}

int combine_indexed(int *p, int n)
{
    return p[0] + fold_indexed(p, n);
}

int main(void)
{
    int a[5];
    int i;
    for (i = 0; i < 5; i++)
        a[i] = i + 1;
    printf("%d %d\n", fold_indexed(a, 5), combine_indexed(a, 5));
    return 0;
}
