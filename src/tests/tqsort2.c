#include <stdio.h>
#include <stdlib.h>
typedef struct { int a; double b; } rec;
int cmp_rec(const void *x, const void *y) {
    return ((const rec *)x)->a - ((const rec *)y)->a;
}
int main(void) {
    rec r[4];
    int i;
    int (*fp)(const void *, const void *);
    r[0].a = 3; r[0].b = 0.1;
    r[1].a = 1; r[1].b = 0.2;
    r[2].a = 4; r[2].b = 0.3;
    r[3].a = 2; r[3].b = 0.4;
    fp = cmp_rec;
    qsort(r, 4, sizeof(rec), fp);
    for (i = 0; i < 4; i++) printf("%d ", r[i].a);
    printf("\n");
    {
        rec key; key.a = 3; key.b = 0;
        rec *p = bsearch(&key, r, 4, sizeof(rec), fp);
        printf("found %d\n", p ? p->a : -1);
    }
    return 0;
}
