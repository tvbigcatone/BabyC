#include <stdio.h>
/* User functions with more than 8 parameters (pointer, double, char,
 * long long beyond the 8th), recursion and parameter writes. Previously the
 * Body-IR tree returned 0 silently for calls with > 8 arguments. */
struct P { int x, y; };
double mix(int a, int b, int c, int d, int e, int f, int g, int h,
           double w, int *out, char tag, long long big)
{
    *out = a + b + c + d + e + f + g + h + (int)tag;
    big += 1;
    return w * 2.0 + (double)(big % 1000);
}
int deep(int n, int a1, int a2, int a3, int a4, int a5, int a6, int a7, int a8, int acc)
{
    if (n == 0)
        return acc + a8;
    a8 = a8 + n;
    return deep(n - 1, a1, a2, a3, a4, a5, a6, a7, a8, acc + a1);
}
int sump(struct P *p, int a, int b, int c, int d, int e, int f, int g, struct P *q)
{
    return p->x + q->y + a + b + c + d + e + f + g;
}
int main(void)
{
    int o = 0;
    struct P p1 = {10, 20}, p2 = {30, 40};
    double r = mix(1, 2, 3, 4, 5, 6, 7, 8, 1.25, &o, 'A', 123456789012LL);
    printf("%.2f %d\n", r, o);
    printf("%d\n", deep(5, 2, 0, 0, 0, 0, 0, 0, 100, 0));
    printf("%d\n", sump(&p1, 1, 1, 1, 1, 1, 1, 1, &p2));
    return 0;
}
