#include <stdio.h>
#include <stdlib.h>

/* Elements with equal keys must keep their input order (stable, deterministic
 * qsort). Sorting by key only; tags record the original position. */
typedef struct { int k; int tag; } rec;
int cmp_k(const void *a, const void *b) { return ((const rec *)a)->k - ((const rec *)b)->k; }

int main(void)
{
    rec r[12];
    int i;
    for (i = 0; i < 12; i++) { r[i].k = i % 3; r[i].tag = i; }
    qsort(r, 12, sizeof(rec), cmp_k);
    for (i = 0; i < 12; i++) printf("%d:%d ", r[i].k, r[i].tag);
    printf("\n");
    return 0;
}
