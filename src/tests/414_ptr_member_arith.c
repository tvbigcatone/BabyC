#include <stdio.h>
struct Slice { int *p; int n; };
int sum_slice(struct Slice *s) {
    int i, t;
    t = 0;
    for (i = 0; i < s->n; i = i + 1)
        t = t + *(s->p + i);
    return t;
}
int main() {
    int buf[8];
    struct Slice s;
    int i, sum, d;
    for (i = 0; i < 8; i = i + 1) buf[i] = (i + 1) * 2;
    s.p = &buf[0];
    s.n = 8;
    sum = sum_slice(&s);
    s.p = s.p + 2;
    d = s.p - &buf[0];
    sum = sum + *s.p;
    printf("%d %d\n", sum, d);
    return 0;
}
