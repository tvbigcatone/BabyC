#include <stdio.h>

int g[3];

void set3(int *x, int *y, int *z) {
    *x = 1; *y = 2; *z = 3;
}

int *slot(int i) { return &g[i]; }

int main() {
    int a, b, c;
    set3(&a, &b, &c);
    printf("%d %d %d\n", a, b, c);
    set3(slot(0), slot(1), slot(2));
    printf("%d %d %d\n", g[0], g[1], g[2]);
    *slot(1) = *slot(0) + *slot(2);
    printf("%d\n", g[1]);
    return 0;
}
