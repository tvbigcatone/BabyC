#include <stdio.h>
/* Generic indexed-load CSE must reuse stable reads and invalidate on aliasing stores. */
static void setfirst(int *q) { *q = 9; }

int main(void)
{
    int a[2];
    int *p;
    int x;
    int i;
    a[0] = 10;
    a[1] = 3;
    p = &a[0];
    x = a[0] + a[0];
    printf("%d ", x);
    *p = 7;
    x = a[0] + a[0];
    printf("%d ", x);
    setfirst(p);
    x = a[0] + a[0];
    printf("%d ", x);
    a[0] = 5;
    x = a[0] + a[1];
    printf("%d ", x);
    i = 0;
    x = a[i] + a[i];
    printf("%d ", x);
    i = 1;
    x = a[i] + a[i];
    printf("%d\n", x);
    return 0;
}
