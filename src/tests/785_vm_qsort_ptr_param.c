#include <stdio.h>
void qs(int *v, int lo, int hi)
{
    int p, i, j, t;
    if (lo >= hi) return;
    p = v[(lo + hi) / 2];
    i = lo; j = hi;
    while (i <= j) {
        while (v[i] < p) i++;
        while (v[j] > p) j--;
        if (i <= j) {
            t = v[i]; v[i] = v[j]; v[j] = t;
            i++; j--;
        }
    }
    qs(v, lo, j);
    qs(v, i, hi);
}
int main() {
    int a[8] = {5,1,4,2,8,3,7,6};
    int i;
    qs(a, 0, 7);
    for (i = 0; i < 8; i++) printf("%d ", a[i]);
    printf("\n");
    return 0;
}
