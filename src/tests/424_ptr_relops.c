#include <stdio.h>
int main() {
    int a[5];
    int *p;
    int *q;
    p = &a[0];
    q = &a[4];
    printf("%d %d %d\n", q - p, p < q ? 1 : 0, p == q ? 1 : 0);
    return 0;
}
