#include <stdio.h>
struct S { int a; int b; };
int main(void) {
    struct S v[4];
    int i, j;
    for (i = 0; i < 4; ++i) {
        v[i].a = i + 1;
        v[i].b = v[i].a * 10;
    }
    i = 1;
    j = 3;
    v[i].b += v[j].a;
    printf("%d %d %d %d %d\n", v[0].b, v[1].b, v[2].a, v[3].a, i);
    return 0;
}
