#include <stdio.h>
#include <stdlib.h>
int cmp_int(const void *a, const void *b) {
    return *(const int *)a - *(const int *)b;
}
int main(void) {
    int a[6] = {5, 1, 4, 2, 6, 3};
    int i;
    int key = 4;
    int *p;
    qsort(a, 6, sizeof(int), cmp_int);
    for (i = 0; i < 6; i++) printf("%d ", a[i]);
    printf("\n");
    p = bsearch(&key, a, 6, sizeof(int), cmp_int);
    printf("found %d\n", p ? *p : -1);
    key = 99;
    p = bsearch(&key, a, 6, sizeof(int), cmp_int);
    printf("miss %d\n", p ? *p : -1);
    return 0;
}
