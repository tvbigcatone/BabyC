#include <stdio.h>
/* Anti-regression: Body-IR pure-memo must not cache functions that
 * touch globals. Second call with same args must still update ctx. */
int ctx = 0;
void upd(int b)
{
    ctx = ctx + b + 1;
}
void upd4(int v)
{
    upd(v); upd(v); upd(v); upd(v);
}
int main()
{
    upd4(10);
    printf("a: %d\n", ctx);
    upd4(7);
    printf("b0: %d\n", ctx);
    upd4(7);
    printf("b1: %d\n", ctx);
    return 0;
}
