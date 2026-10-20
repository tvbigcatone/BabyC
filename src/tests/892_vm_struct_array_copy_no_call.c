#include <stdio.h>
struct S { int a; int b; };
int main(void) {
    struct S v[2];
    struct S x;
    x.a = 5;
    x.b = 9;
    v[0] = x;
    v[1] = v[0];
    v[1].b += 4;
    printf("%d %d %d %d\n", v[0].a, v[0].b, v[1].a, v[1].b);
    return 0;
}
