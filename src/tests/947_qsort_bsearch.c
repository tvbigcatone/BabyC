#include <stdio.h>
#include <stdlib.h>

/* qsort/bsearch with an interpreted comparator passed by name. In the
 * Body-IR path the function name used to reach the library as an integer, so
 * the comparator was never called and the array came back scrambled. */
typedef struct { int k; int tag; } rec;

int cmp_int(const void *a, const void *b) { return *(const int *)a - *(const int *)b; }
int cmp_rec(const void *a, const void *b) { return ((const rec *)a)->k - ((const rec *)b)->k; }

int main(void)
{
    int v[8] = {5, 2, 9, 1, 7, 3, 8, 0};
    rec r[6];
    int i, key = 7, *p;
    for (i = 0; i < 6; i++) { r[i].k = (i * 5) % 6; r[i].tag = i; }

    qsort(v, 8, sizeof(int), cmp_int);
    for (i = 0; i < 8; i++) printf("%d ", v[i]);
    printf("\n");

    p = bsearch(&key, v, 8, sizeof(int), cmp_int);
    printf("%d\n", p ? (int)(p - v) : -1);
    key = 4;
    p = bsearch(&key, v, 8, sizeof(int), cmp_int);
    printf("%d\n", p ? (int)(p - v) : -1);

    qsort(r, 6, sizeof(rec), cmp_rec);
    for (i = 0; i < 6; i++) printf("%d:%d ", r[i].k, r[i].tag);
    printf("\n");
    return 0;
}
