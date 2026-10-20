/* Anti-regression: p->m = v / s.m = v (Body IR lvalue complete) */
#include <stdio.h>
struct S { int a; int b; };
void set(struct S *p, int x, int y) {
    p->a = x;
    p->b = y;
}
int main() {
    struct S s;
    s.a = 0; s.b = 0;
    set(&s, 11, 22);
    printf("%d %d\n", s.a, s.b);
    return 0;
}
