#include <stdio.h>
#include <stdlib.h>
int calls;
int cmp_int(const void *a, const void *b) {
    int xa = *(const int *)a;
    int xb = *(const int *)b;
    calls++;
    printf("cmp %d %d\n", xa, xb);
    return xa - xb;
}
int main(void) {
    int a[3] = {3, 1, 2};
    int i;
    calls = 0;
    qsort(a, 3, sizeof(int), cmp_int);
    printf("calls=%d result=", calls);
    for (i = 0; i < 3; i++) printf("%d ", a[i]);
    printf("\n");
    return 0;
}
