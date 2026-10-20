#include <stdio.h>
#include <stdlib.h>
/* (T *)expr is a pointer whatever T is. The IR typed (double *)malloc(..)
 * as a double (linear converted the address to FP bits -> SEGV on q[1] = ..)
 * and (char *)p as an 8-bit integer. calloc/realloc as linear host calls;
 * double *q locals indexed as doubles in the tree. */
int main(void)
{
    double *q = (double *)malloc(4 * sizeof(double));
    int *a = (int *)calloc(4, sizeof(int));
    int arr[4] = {10, 20, 30, 40};
    char *c0 = (char *)&arr[0];
    char *c3 = (char *)&arr[3];
    long s = 0;
    int i, j = 2;
    q[1] = 1.5;
    q[j] = j * 0.25;
    q[3] = j;
    q[0] = q[1] + q[3];
    printf("%.2f %.2f %.2f %.2f\n", q[0], q[1], q[2], q[3]);
    for (i = 0; i < 4; i++) a[i] += i * 3;
    a = (int *)realloc(a, 8 * sizeof(int));
    for (i = 4; i < 8; i++) a[i] = a[i - 4] + 1;
    for (i = 0; i < 8; i++) s += a[i];
    printf("%ld %d\n", s, a[7]);
    printf("%d %d %d\n", (int)(c3 - c0), *(int *)c3, *(int *)(c0 + 4));
    free(a);
    free(q);
    return 0;
}
