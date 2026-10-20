#include <stdio.h>
typedef struct { int a; int b; } S;
int main() {
    S s;
    S *p;
    s.a = 1; s.b = 2;
    p = &s;
    printf("%d %d\n", p->a, p->b);
    return 0;
}
