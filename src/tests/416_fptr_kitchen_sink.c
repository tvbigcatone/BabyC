#include <stdio.h>
int a0(int x) { return x; }
int a1(int x) { return x + 1; }
int a2(int x) { return x + 2; }
struct Ctx {
    int (*f0)(int);
    int (*f1)(int);
    int (*f2)(int);
    int *base;
    int n;
};
int dispatch(struct Ctx *c, int idx, int v) {
    int (*fp)(int);
    if (idx == 0) fp = c->f0;
    else if (idx == 1) fp = c->f1;
    else fp = c->f2;
    return fp(v) + *(c->base + (idx & 1));
}
int fold_a1(struct Ctx *c) {
    int i, s;
    int (*fp)(int);
    s = 0;
    fp = c->f1;
    for (i = 0; i < c->n; i = i + 1)
        s = s + fp(*(c->base + i));
    return s;
}
int main() {
    int buf[4];
    struct Ctx c;
    int i, s;
    for (i = 0; i < 4; i = i + 1) buf[i] = i + 1;
    c.f0 = a0; c.f1 = a1; c.f2 = a2;
    c.base = &buf[0];
    c.n = 4;
    s = dispatch(&c, 0, 10);
    s = s + dispatch(&c, 2, 5);
    s = s + fold_a1(&c);
    printf("%d\n", s);
    return 0;
}
