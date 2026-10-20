/* Anti-regression: p->m / s.m in function (Body IR or token) */
#include <stdio.h>
struct S { int a; int b; };
int sum(struct S *p) {
    return p->a + p->b;
}
int main() {
    struct S s;
    s.a = 10;
    s.b = 20;
    printf("%d %d\n", s.a, sum(&s));
    return 0;
}
