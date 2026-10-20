#include <stdio.h>

struct Cell { char tag; volatile int value; };
struct Inner { int lead; struct Cell cell; };
struct Outer { char head; struct Inner inner; };
struct Outer global_outer;

void main()
{
    struct Outer local_outer;
    int global_post, global_pre, local_post, local_pre;

    global_outer.inner.cell.value = 3;
    global_post = global_outer.inner.cell.value++;
    global_pre = ++global_outer.inner.cell.value;

    local_outer.inner.cell.value = 7;
    local_post = local_outer.inner.cell.value++;
    local_pre = ++local_outer.inner.cell.value;

    printf("%d %d %d %d %d %d\n", global_outer.inner.cell.value,
           global_post, global_pre, local_outer.inner.cell.value,
           local_post, local_pre);
}
