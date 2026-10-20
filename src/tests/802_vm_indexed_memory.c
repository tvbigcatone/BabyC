#include <stdio.h>

static int a[4] = { 3, 5, 7, 11 };

static void copy_at(int *p, int src, int dst)
{
    int i;
    i = 0;
    while (i < 1)
    {
        if (src >= 0 && dst >= 0)
            p[dst] = p[src];
        i++;
    }
}

static int sum_and_update(int *p, int n)
{
    int i, sum;
    i = 0;
    sum = 0;
    while (i < n)
    {
        sum += p[i];
        p[i] = p[i] + 1;
        i++;
    }
    return sum;
}

int main(void)
{
    int sum;
    sum = sum_and_update(a, 4);
    copy_at(a, 0, 3);
    printf("%d %d %d %d %d\n", sum, a[0], a[1], a[2], a[3]);
    return 0;
}
