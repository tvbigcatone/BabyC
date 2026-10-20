/* Outer for with Decl + 2 inner for thin-call bodies */
#include <stdio.h>
static int gq[16], gh, gt, gc, stalled;
static long long acc;
static int push(int v) {
    if (gc >= 16) return 0;
    gq[gt] = v; gt = (gt + 1) % 16; gc++; return 1;
}
static int pop(int *out) {
    if (gc <= 0) return 0;
    *out = gq[gh]; gh = (gh + 1) % 16; gc--; return 1;
}
int main(void) {
    int i, k, v;
    gh = gt = gc = stalled = 0; acc = 0;
    for (i = 0; i < 20; i++) {
        int k;
        for (k = 0; k < 1 + (i % 3); k++)
            if (!push((i * 3 + k) % 50)) stalled++;
        for (k = 0; k < 2; k++) {
            if (pop(&v)) acc += v;
            else break;
        }
    }
    while (pop(&v)) acc += v;
    printf("%lld %d\n", acc, stalled);
    return 0;
}
