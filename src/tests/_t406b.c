#include <stdio.h>
int main() {
    int a[5];
    int *p = &a[0];
    int *q = &a[4];
    int d = q - p;
    int lt = p < q ? 1 : 0;
    int eq = p == q ? 1 : 0;
    printf("%d %d %d\n", d, lt, eq);
    return 0;
}
