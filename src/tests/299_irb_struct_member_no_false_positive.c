#include <stdio.h>

/* Phase A safety check: the flip side of 281 -- reading a field that
 * *is* initialized on a struct where a sibling field is not must NOT
 * trip the uninitialized-variable check (per-byte bitmap, not a single
 * whole-struct flag). */

struct box {
    int x;
    int y;
};

void main()
{
    struct box b;
    int i, t;

    b.y = 1; /* x is left uninitialized on purpose; only y is read below */

    t = 0;
    for (i = 0; i < 5; i++)
        t = t + b.y;

    printf("t %d\n", t);
}
