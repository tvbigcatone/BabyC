/* direct a.fp(x) and p->fp(x) member function-pointer calls */
#include <stdio.h>
int add1(int x) { return x + 1; }
struct Ops { int (*fp)(int); int tag; };
int call(struct Ops *p) { return p->fp(p->tag); }
int main() {
    struct Ops a;
    a.fp = add1;
    a.tag = 10;
    printf("%d\n", a.fp(a.tag));
    printf("%d\n", call(&a));
    return 0;
}
