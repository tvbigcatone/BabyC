#include <stdio.h>
void set_pair(int *a, int *b, int x, int y) {
    *a = x;
    *b = y;
}
void swap_ptr(int **a, int **b) {
    int *t;
    t = *a;
    *a = *b;
    *b = t;
}
int main(void) {
    int x, y;
    int row[3];
    int *p1, *p2;
    int i;
    for (i = 0; i < 3; i = i + 1)
        row[i] = i + 1;
    set_pair(&x, &y, 10, 20);
    p1 = &x; p2 = &y;
    swap_ptr(&p1, &p2);
    printf("%d %d %d %d %d\n", *p1, *p2, row[0], row[1], row[2]);
    return 0;
}
