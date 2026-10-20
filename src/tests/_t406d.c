#include <stdio.h>
int main() {
    int a[5];
    int *p = &a[0];
    int *q = &a[4];
    printf("%d %d %d\n", 4, 1, p == q ? 1 : 0);
    return 0;
}
