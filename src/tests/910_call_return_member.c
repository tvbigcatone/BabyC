/* Anti-regression: ret()->m load/store and &ret()->m under vm_strict. */
#include <stdio.h>
struct S { int x; int y; };
struct S gs;
struct S *ret_s(void) { return &gs; }
int main(void) {
    int *p;
    gs.x = 0; gs.y = 0;
    ret_s()->x = 10;
    ret_s()->y = ret_s()->x + 5;
    p = &ret_s()->x;
    *p = *p + 1;
    printf("%d %d\n", gs.x, gs.y);
    return 0;
}
