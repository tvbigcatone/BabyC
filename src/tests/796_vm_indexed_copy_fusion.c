#include <stdio.h>

static int copy_at(int *p, int dst, int src, int n)
{
    int k = 0;
    int sum = 0;
    while (k < n) {
        p[dst] = p[src];
        sum = sum + p[dst];
        k = k + 1;
    }
    return sum;
}

int main(void)
{
    int a[4] = {10, 20, 30, 40};
    int sum = copy_at(a, 1, 3, 3);
    printf("%d %d %d %d %d\n", a[0], a[1], a[2], a[3], sum);
    return 0;
}
