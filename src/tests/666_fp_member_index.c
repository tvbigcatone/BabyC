#include <stdio.h>
typedef struct { int vals[4]; int tag; } Box;
int sum_box(Box *b) {
    int i, s = 0;
    for (i = 0; i < 4; i++)
        s = s + b->vals[i];
    return s + b->tag;
}
int main() {
    Box x;
    int i, t = 0;
    for (i = 0; i < 4; i++) x.vals[i] = i + 1;
    x.tag = 10;
    for (i = 0; i < 50; i++)
        t = t + sum_box(&x);
    printf("%d\n", t);
    return 0;
}
