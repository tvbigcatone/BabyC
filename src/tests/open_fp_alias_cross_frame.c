#include <stdio.h>

static void update(float *pf, double *pd, int *pi, float **ppf)
{
    *pf = 2.25f;
    *pd = 3.75;
    *pi = 17;
    **ppf = 4.5f;
}

static void nested(float *p, int n)
{
    if (n == 0) {
        *p = 6.25f;
        return;
    }
    nested(p, n - 1);
}

int main(void)
{
    float f = 1.0f;
    double d = 2.0;
    int i = 3;
    float *pf = &f;

    update(&f, &d, &i, &pf);
    printf("f=%.2f d=%.2f i=%d\n", f, d, i);
    nested(pf, 2);
    printf("nested=%.2f\n", f);
    return 0;
}
