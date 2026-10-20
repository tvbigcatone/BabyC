#include <stdio.h>
struct S { int a; int b; };
int main(void) {
    struct S v[3];
    int i = 1;
    v[0].a = 1;
    v[0].b = 2;
    v[i].a = 7;
    v[i].b = v[i].a + 5;
    i = 2;
    v[i].a = v[1].b + 3;
    v[i].b = v[i].a + v[1].a;
    printf("%d %d %d %d %d %d\n", v[0].a, v[1].a, v[1].b, v[2].a, v[2].b, i);
    return 0;
}
