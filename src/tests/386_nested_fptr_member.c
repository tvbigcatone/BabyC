#include <stdio.h>
struct Inner { int (*f)(int); int k; };
struct Outer { struct Inner in; int n; };
int twice(int x) { return x * 2; }
int thrice(int x) { return x * 3; }
int apply(struct Outer *o, int x)
{
    return o->in.f(x + o->in.k) + o->n;
}
int main()
{
    struct Outer o;
    o.in.f = twice;
    o.in.k = 1;
    o.n = 10;
    printf("%d\n", apply(&o, 5));
    o.in.f = thrice;
    printf("%d\n", apply(&o, 5));
    return 0;
}
