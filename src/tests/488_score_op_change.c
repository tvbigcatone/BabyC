#include <stdio.h>
struct Obj {
    int a, b, c;
    int (*op)(int, int);
};
int add(int x, int y) { return x + y; }
int mul(int x, int y) { return x * y; }
int score(struct Obj *o) {
    int s;
    s = o->a + o->b + o->c;
    s = o->op(s, o->a);
    return s;
}
struct Obj o;
int main() {
    int i, t;
    t = 0;
    o.a = 2; o.b = 3; o.c = 5; o.op = add;
    for (i = 0; i < 10; i = i + 1)
        t = t + score(&o);
    o.op = mul;
    t = t + score(&o);
    printf("%d\n", t);
    return 0;
}
