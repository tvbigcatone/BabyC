/* p->fp(x) arrow form */
#include <stdio.h>
int mul2(int x) { return x * 2; }
struct S { int (*fp)(int); };
int main() {
    struct S s;
    struct S *p;
    s.fp = mul2;
    p = &s;
    printf("%d\n", p->fp(7));
    return 0;
}
