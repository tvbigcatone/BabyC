#include <stdio.h>
int add1(int x) { return x + 1; }
int add2(int x) { return x + 2; }
struct Ops { int (*fp)(int); int tag; };
int main() {
    struct Ops a;
    struct Ops b;
    struct Ops *p;
    int (*fp)(int);
    int s = 0;
    a.fp = add1;
    a.tag = 10;
    b.fp = add2;
    b.tag = 20;
    p = &a;
    fp = p->fp;
    s = s + fp(p->tag);
    p = &b;
    fp = p->fp;
    s = s + fp(3);
    printf("%d\n", s);
    return 0;
}
