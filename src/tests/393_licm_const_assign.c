#include <stdio.h>
/* ISOLATED: LICM hoist local=const out of for body */
int main()
{
    int i, x, s;
    s = 0;
    for (i = 0; i < 5; i++)
    {
        x = 10; /* invariant — may be hoisted */
        s = s + x + i;
    }
    printf("%d %d\n", s, x);
    return 0;
}
