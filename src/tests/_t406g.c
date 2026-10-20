#include <stdio.h>
int main() {
    int a[5];
    int *p = &a[0];
    int *q = &a[4];
    int x = p < q ? 1 : 0;
    int y = p < q ? 1 : 0;
    printf("%d %d\n", x, y);
    return 0;
}
