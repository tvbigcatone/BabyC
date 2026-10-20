#include <stdio.h>

int x, y;
int *px;

int **ret_pp(void) { return &px; }
int *ret_px(void) { return px; }

int main() {
    int **pp;
    x = 1; y = 2;
    px = &x;
    pp = ret_pp();
    printf("%d\n", **pp);
    *pp = &y;
    printf("%d\n", *px);
    *ret_pp() = &x;
    printf("%d\n", *ret_px());
    *ret_px() = 99;
    printf("%d\n", x);
    **ret_pp() = 77;
    printf("%d\n", x);
    return 0;
}
