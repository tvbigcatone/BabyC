#include <stdio.h>
struct S { int *p; };
int main() {
    int a[2];
    struct S s;
    a[0] = 0; a[1] = 0;
    s.p = &a[0];
    *s.p = 11;
    s.p = s.p + 1;
    *s.p = 22;
    printf("%d %d\n", a[0], a[1]);
    return 0;
}
