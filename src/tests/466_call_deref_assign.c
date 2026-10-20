#include <stdio.h>

int g;
struct S { int a; int b; };
struct S gs;

int *pg(void) { return &g; }
struct S *ps(void) { return &gs; }

int main() {
    *pg() = 10;
    printf("%d\n", g);
    ps()->a = 20;
    ps()->b = 30;
    printf("%d %d\n", gs.a, gs.b);
    (*ps()).a = 40;
    printf("%d\n", gs.a);
    return 0;
}
