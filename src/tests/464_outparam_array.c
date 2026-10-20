#include <stdio.h>

void set_pair(int *a, int *b, int x, int y) {
    *a = x;
    *b = y;
}

void set_row(int *row, int n, int v) {
    int i;
    for (i = 0; i < n; i = i + 1)
        row[i] = v + i;
}

void swap_ptr(int **a, int **b) {
    int *t;
    t = *a;
    *a = *b;
    *b = t;
}

int main() {
    int x, y;
    int row[3];
    int *p1, *p2;
    set_pair(&x, &y, 10, 20);
    printf("%d %d\n", x, y);
    set_row(row, 3, 5);
    printf("%d %d %d\n", row[0], row[1], row[2]);
    p1 = &x; p2 = &y;
    swap_ptr(&p1, &p2);
    printf("%d %d\n", *p1, *p2);
    return 0;
}
