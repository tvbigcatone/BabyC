#include <stdio.h>
int main() {
    int a[10];
    int *p = &a[2];
    int *q = &a[7];
    int *r = &a[2];
    printf("%d %d %d %d\n",
        (int)(q - p),
        p < q ? 1 : 0,
        p == r ? 1 : 0,
        p != q ? 1 : 0);
    return 0;
}
