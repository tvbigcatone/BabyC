#include <stdio.h>
/* Call + member hot path: score() reads struct fields and calls through member fptr. */
struct Obj {
    int a, b, c;
    int (*op)(int, int);
};
int add(int x, int y) { return x + y; }
int mul(int x, int y) { return x * y; }
int score(struct Obj *o)
{
    int s = o->a + o->b + o->c;
    s = o->op(s, o->a);
    return s;
}
int main()
{
    struct Obj o;
    int i, t = 0;
    o.a = 2; o.b = 3; o.c = 5; o.op = add;
    for (i = 0; i < 1000; i++)
        t += score(&o);
    o.op = mul;
    t += score(&o);
    printf("%d\n", t);
    return 0;
}
