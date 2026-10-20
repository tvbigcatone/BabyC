#include <stdio.h>

/* Anti-regression: nested struct member access (outer.inner.field) chains
 * two '.' accesses that Irb cannot parse at all, so the loop must fall
 * back to the token / Loop Body IR path and still compute correctly. */

struct inner {
    int v;
};

struct outer {
    struct inner in;
};

void main()
{
    struct outer o;
    int i;

    o.in.v = 0;

    for (i = 0; i < 12; i++)
        o.in.v = o.in.v + 5;

    printf("s %d\n", o.in.v);
}
