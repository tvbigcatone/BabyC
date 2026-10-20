#include <stdio.h>
int main() {
    int a[5];
    int *p = &a[0];
    int *q = &a[4];
    int eq = (p == q);
    printf("%d\n", eq);
    printf("%d\n", p == q);
    return 0;
}
