#include <stdio.h>
/* Global array index must compile to Body-IR (not token fallback). */
int tab[8];
int get(int i) { return tab[i]; }
int set(int i, int v) { tab[i] = v; return v; }
int sum_range(int n) {
    int i, s = 0;
    for (i = 0; i < n; i = i + 1)
        s = s + tab[i];
    return s;
}
int main() {
    int i;
    for (i = 0; i < 5; i = i + 1)
        set(i, i * 10);
    printf("%d %d\n", get(3), sum_range(5));
    return 0;
}
