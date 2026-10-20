#include <stdio.h>
void fill(int *p, int n)
{
    if (n <= 0) return;
    p[n - 1] = n + (n > 1 ? p[n - 2] : 0);
    fill(p, n - 1);
    p[n - 1] += n;
}
int main(void)
{
    int a[6] = {0,0,0,0,0,0};
    int i, sum = 0;
    fill(a, 6);
    for (i = 0; i < 6; ++i) sum += a[i];
    printf("%d %d %d %d %d %d %d\n", a[0],a[1],a[2],a[3],a[4],a[5],sum);
    return 0;
}
