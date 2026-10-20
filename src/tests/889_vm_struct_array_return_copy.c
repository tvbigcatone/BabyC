#include <stdio.h>
struct Wide { int a; int b; int c; int d; };
struct Wide make_wide(int x) {
    struct Wide v;
    v.a = x;
    v.b = x + 1;
    v.c = x + 2;
    v.d = x + 3;
    return v;
}
int main(void) {
    struct Wide values[3];
    values[0] = make_wide(10);
    values[1] = make_wide(20);
    values[2] = values[0];
    values[2].d += 100;
    printf("%d %d %d %d %d %d\n",
        values[0].a, values[0].d, values[1].a, values[1].d,
        values[2].a, values[2].d);
    return 0;
}
