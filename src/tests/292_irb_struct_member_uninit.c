#include <stdio.h>

/* Phase A safety check: the per-byte ElemInit bitmap now also tracks
 * struct roots (extended from "TypeArray only" to "TypeArray or
 * TypeStruct" in IrbLocLoad/IrbLocStore). Reading an uninitialized field
 * of an otherwise partially-initialized struct, inside a loop, must be
 * caught -- with the same diagnostic whether Irb is engaged or not --
 * not silently read as garbage. */

struct box {
    int x;
    int y;
};

void main()
{
    struct box b;
    int i, t;

    b.y = 1; /* only y is set; x stays uninitialized */

    t = 0;
    for (i = 0; i < 3; i++)
        t = t + b.x;

    printf("t %d\n", t);
}
