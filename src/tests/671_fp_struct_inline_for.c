/* Structural for-inline of thin callee: if(!call) and if(call)else break */
#include <stdio.h>
static int g_n, g_acc;
static int add1(int x) {
    g_n++;
    g_acc = g_acc + x;
    return 1;
}
static int take(int *out) {
    if (g_n <= 0) return 0;
    *out = g_acc;
    g_n--;
    return 1;
}
int main(void) {
    int i, v, s, miss;
    g_n = 0;
    g_acc = 0;
    miss = 0;
    for (i = 0; i < 5; i++) {
        if (!add1(i + 1)) miss++;
    }
    s = 0;
    for (i = 0; i < 10; i++) {
        if (take(&v)) s = s + v;
        else break;
    }
    printf("%d %d %d %d\n", g_acc, miss, s, g_n);
    return 0;
}
