#include <stdio.h>

void qsort_r(int *a, int lo, int hi) {
    int p, i, j, pivot, t;
    if (lo >= hi) return;
    pivot = a[hi];
    i = lo;
    for (j = lo; j < hi; j = j + 1) {
        if (a[j] < pivot) {
            t = a[i]; a[i] = a[j]; a[j] = t;
            i = i + 1;
        }
    }
    t = a[i]; a[i] = a[hi]; a[hi] = t;
    qsort_r(a, lo, i - 1);
    qsort_r(a, i + 1, hi);
}

int main() {
    int a[10];
    int i;
    a[0]=9;a[1]=3;a[2]=7;a[3]=1;a[4]=8;
    a[5]=2;a[6]=6;a[7]=4;a[8]=5;a[9]=0;
    qsort_r(a, 0, 9);
    i = 0;
    while (i < 10) {
        if (i) printf(" ");
        printf("%d", a[i]);
        i = i + 1;
    }
    printf("\n");
    return 0;
}
