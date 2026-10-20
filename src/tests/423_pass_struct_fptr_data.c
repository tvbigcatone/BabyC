#include <stdio.h>
int dbl(int x) { return x * 2; }
struct H { int (*op)(int); int *data; int n; };
int fold(struct H *h) {
    int i, s;
    int (*fp)(int);
    s = 0;
    fp = h->op;
    for (i = 0; i < h->n; i = i + 1)
        s = s + fp(*(h->data + i));
    return s;
}
int main() {
    int b[4];
    struct H h;
    int i;
    for (i = 0; i < 4; i = i + 1) b[i] = i + 1;
    h.op = dbl;
    h.data = &b[0];
    h.n = 4;
    printf("%d\n", fold(&h));
    return 0;
}
