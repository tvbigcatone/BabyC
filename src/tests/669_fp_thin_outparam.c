/* Scalar out-param *out = v — thin kernel path (no name match) */
#include <stdio.h>
static int gq[8];
static int gh, gt, gc;
static int push2(int v) {
    if (gc >= 8) return 0;
    gq[gt] = v;
    gt = (gt + 1) % 8;
    gc++;
    return 1;
}
static int pop2(int *out) {
    if (gc <= 0) return 0;
    *out = gq[gh];
    gh = (gh + 1) % 8;
    gc--;
    return 1;
}
int main(void) {
    int i, v, s;
    gh = gt = gc = 0;
    s = 0;
    for (i = 0; i < 5; i++)
        push2(i * 10);
    while (pop2(&v))
        s = s + v;
    printf("%d %d\n", s, gc);
    return 0;
}
