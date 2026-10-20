#include <stdio.h>
int main() {
    int a[5];
    int *p;
    int *q;
    p = &a[0];
    q = &a[4];
    printf("%d\n", (int)(q - p));
    printf("%d\n", p < q ? 1 : 0);
    printf("%d\n", p == q ? 1 : 0);
    return 0;
}
