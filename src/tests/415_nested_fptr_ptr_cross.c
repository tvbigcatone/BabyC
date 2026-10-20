#include <stdio.h>
int neg(int x) { return 0 - x; }
int id(int x) { return x; }
struct Handler {
    int (*op)(int);
    int *data;
    int len;
};
int run(struct Handler *h) {
    int i, s;
    int (*fp)(int);
    s = 0;
    fp = h->op;
    for (i = 0; i < h->len; i = i + 1)
        s = s + fp(*(h->data + i));
    return s;
}
int main() {
    int a[4];
    struct Handler h;
    int i, r;
    int (*fp)(int);
    for (i = 0; i < 4; i = i + 1) a[i] = i + 1;
    fp = id;
    h.op = fp;
    h.data = &a[0];
    h.len = 4;
    r = run(&h);
    fp = neg;
    h.op = fp;
    r = r + run(&h);
    printf("%d\n", r);
    return 0;
}
