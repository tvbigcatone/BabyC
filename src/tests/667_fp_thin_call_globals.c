/* Structural: multi-stmt function using only scalar params + globals
 * must take thin-kernel call path (no name-based matching). */
#include <stdio.h>
static int g_cnt;
static int g_sum;
static int bump(int v) {
    if (v < 0) return 0;
    g_cnt++;
    g_sum = g_sum + v;
    return 1;
}
static int fold(int n) {
    int i, ok;
    ok = 0;
    for (i = 0; i < n; i++)
        ok = ok + bump(i);
    return ok;
}
int main(void) {
    g_cnt = 0;
    g_sum = 0;
    printf("%d %d %d\n", fold(5), g_cnt, g_sum);
    return 0;
}
