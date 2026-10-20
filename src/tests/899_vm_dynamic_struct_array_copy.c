#include <stdio.h>
struct S { int a; int b; };
int main(void) {
    struct S v[3];
    int i = 2;
    int j = 0;
    v[0].a = 5; v[0].b = 9;
    v[1].a = 7; v[1].b = 11;
    v[2].a = 13; v[2].b = 17;
    v[i] = v[j];
    v[i] = v[i]; /* self-assignment must remain well-defined */
    v[i].b += 4;
    printf("%d %d %d %d %d %d\n", v[0].a, v[0].b, v[1].a, v[1].b, v[2].a, v[2].b);
    return 0;
}
