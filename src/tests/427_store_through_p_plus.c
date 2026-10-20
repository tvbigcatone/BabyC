#include <stdio.h>
int main() {
    int a[3];
    int *p;
    a[0] = 0; a[1] = 0; a[2] = 0;
    p = &a[0];
    *(p + 1) = 7;
    *(p + 2) = 9;
    *p = 3;
    printf("%d %d %d\n", a[0], a[1], a[2]);
    return 0;
}
