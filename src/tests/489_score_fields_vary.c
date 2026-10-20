#include <stdio.h>
struct Obj {
    int a, b, c;
    int (*op)(int, int);
};
int add(int x, int y) { return x + y; }
int score(struct Obj *o) {
    int s;
    s = o->a + o->b + o->c;
    s = o->op(s, o->a);
    return s;
}
struct Obj o;
int main() {
    o.a = 1; o.b = 1; o.c = 1; o.op = add;
    printf("%d\n", score(&o));
    o.a = 10; o.b = 20; o.c = 30;
    printf("%d\n", score(&o));
    return 0;
}
