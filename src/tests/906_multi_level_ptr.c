/* Anti-regression: int** load/store under vm_strict (**pp). */
#include <stdio.h>
int main(void) {
    int x = 3;
    int y = 9;
    int *p = &x;
    int **pp = &p;
    printf("%d\n", **pp);
    **pp = **pp + 5;
    printf("%d %d\n", x, *p);
    *pp = &y;
    printf("%d\n", **pp);
    return 0;
}
