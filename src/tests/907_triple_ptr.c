/* Anti-regression: int*** load/store under vm_strict. */
#include <stdio.h>
int main(void) {
    int x = 4;
    int *p = &x;
    int **pp = &p;
    int ***ppp = &pp;
    printf("%d\n", ***ppp);
    ***ppp = ***ppp + 3;
    printf("%d %d\n", x, *p);
    return 0;
}
