#include <stdio.h>
int main() {
    int a[3];
    int *p;
    int x;
    a[0] = 1; a[1] = 2; a[2] = 3;
    p = &a[0];
    x = (*p, p = p + 1, *p);
    printf("%d %d\n", x, *p);
    return 0;
}
