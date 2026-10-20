/* Nested fptr: table + struct member dispatch. Anti-regression for call+member. */
#include <stdio.h>

int add1(int x) { return x + 1; }
int add2(int x) { return x + 2; }

typedef int (*fn_t)(int);

struct Ops {
    fn_t f;
};

int apply(struct Ops *o, int x)
{
    return o->f(x);
}

int main()
{
    fn_t tab[2];
    struct Ops a, b;
    int i, s;

    tab[0] = add1;
    tab[1] = add2;
    a.f = tab[0];
    b.f = tab[1];
    s = 0;
    for (i = 0; i < 3; i++)
        s = s + apply(&a, i) + apply(&b, i) + tab[i & 1](i);
    printf("%d\n", s);
    return 0;
}
