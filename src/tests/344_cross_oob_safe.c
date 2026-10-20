/* Cross: recursion + array in-bounds after hanoi-class work */
#include <stdio.h>
int get(int *a, int i, int n) {
    if (i < 0 || i >= n) return -1;
    return a[i];
}
int main() {
    int a[4];
    int i;
    for (i = 0; i < 4; i++) a[i] = i * 10;
    printf("%d %d %d\n", get(a, 0, 4), get(a, 3, 4), get(a, 4, 4));
    return 0;
}
