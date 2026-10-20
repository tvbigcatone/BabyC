#include <stdio.h>

int add1(int x) { return x + 1; }
int mul2(int x) { return x * 2; }

struct Ops {
    int (*fn)(int);
    int tag;
};

int main()
{
    struct Ops o;
    struct Ops *p;
    int r;

    o.tag = 7;
    o.fn = &add1;
    r = (*o.fn)(10);
    printf("dot %d tag %d\n", r, o.tag);

    o.fn = &mul2;
    p = &o;
    r = (*p->fn)(10);
    printf("arrow %d\n", r);

    printf("ok\n");
    return 0;
}
