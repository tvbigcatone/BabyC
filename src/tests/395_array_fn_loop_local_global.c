#include <stdio.h>
/* CROSS HEAVY: array→fn in loop, local/global, . -> & * indices */
typedef struct {
    int n;
    int a[4];
} Box;

Box G;
int gsum;

int sum4(int *p, int n)
{
    int i, s;
    s = 0;
    for (i = 0; i < n; i++)
        s = s + p[i];
    return s;
}

int sum_via_ptr(int *p)
{
    return *p + *(p + 1);
}

void fill(int *dst, int v0, int v1, int v2, int v3)
{
    dst[0] = v0;
    dst[1] = v1;
    dst[2] = v2;
    dst[3] = v3;
}

int main()
{
    Box L;
    int i, t, u;
    int *pp;
    int local[4];

    G.n = 4;
    fill(G.a, 1, 2, 3, 4);
    L.n = 4;
    fill(L.a, 10, 20, 30, 40);

    gsum = 0;
    for (i = 0; i < G.n; i++)
        gsum = gsum + G.a[i];

    t = sum4(G.a, G.n);
    u = sum4(L.a, L.n);

    /* pointer walk on global via &G.a[0] */
    pp = &G.a[0];
    t = t + sum_via_ptr(pp);

    /* local array filled in loop, passed to fn */
    for (i = 0; i < 4; i++)
        local[i] = i * 3;
    u = u + sum4(local, 4);

    /* member via pointer */
    {
        Box *bp;
        bp = &L;
        t = t + bp->a[0] + (*bp).a[1];
    }

    printf("%d %d %d %d\n", gsum, t, u, L.a[3]);
    return 0;
}
