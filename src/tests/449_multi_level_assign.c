#include <stdio.h>
int main() {
    int a, b, c;
    int *p;
    int **pp;
    a = 1; b = 2; c = 3;
    p = &a;
    pp = &p;
    **pp = 9;
    p = &b;
    *p = *p + c;
    printf("%d\n", a);
    printf("%d\n", b);
    return 0;
}
