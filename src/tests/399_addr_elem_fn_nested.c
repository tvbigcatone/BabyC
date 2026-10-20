#include <stdio.h>
/* CROSS: &arr[i] into fn, nested loops, global/local arrays */
int G[5];

void add1(int *x)
{
    *x = *x + 1;
}

int sum_range(int *base, int n)
{
    int i, s;
    s = 0;
    for (i = 0; i < n; i++)
        s = s + *(base + i);
    return s;
}

int main()
{
    int L[5];
    int i, j, s;

    for (i = 0; i < 5; i++)
    {
        G[i] = i;
        L[i] = i * 2;
    }

    for (i = 0; i < 5; i++)
        add1(&G[i]);

    for (i = 0; i < 3; i++)
        for (j = 0; j < 2; j++)
            add1(&L[i + j]);

    s = sum_range(&G[0], 5) + sum_range(L, 5);
    printf("%d %d %d\n", G[0], L[2], s);
    return 0;
}
