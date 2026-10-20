#include <stdio.h>
/* Ternary ?: in Body-IR */
int max2(int a, int b) { return a > b ? a : b; }
int abs1(int x) { return x < 0 ? -x : x; }
int clamp(int x, int lo, int hi) {
    return x < lo ? lo : (x > hi ? hi : x);
}
int main() {
    printf("%d %d %d\n", max2(3, 7), abs1(-5), clamp(12, 0, 10));
    printf("%d\n", clamp(-2, 0, 10));
    return 0;
}
