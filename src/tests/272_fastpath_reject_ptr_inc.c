#include <stdio.h>

/* Anti-regression: a loop-local pointer variable used only as a scalar
 * (not array-indexed) is not an int-base type, so Irb must refuse to bind
 * it (IrbBind: !IrbIsIntBase(Base)) and fall back to the token / Loop
 * Body IR path.  Correct pointer arithmetic (see 264/265) must still run. */

void main()
{
    char s[8];
    char *p;
    int i, n;

    s[0] = 'a'; s[1] = 'b'; s[2] = 'c'; s[3] = 0;

    n = 0;
    p = s;
    for (i = 0; i < 3; i++) {
        n = n + 1;
        p++;
    }
    printf("for %d\n", n);

    n = 0;
    p = s;
    while (n < 3) {
        n = n + 1;
        p++;
    }
    printf("while %d\n", n);

    printf("ok\n");
}
