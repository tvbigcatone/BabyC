#include <stdio.h>

/* Phase A: IrbSlot's key became (Name, Field) instead of just Name, so
 * that two different members of the SAME struct variable used in the
 * same loop body get two separate bindings instead of colliding into
 * one (which would silently read/write the wrong field). */

struct pair {
    int a;
    int b;
};

void main()
{
    struct pair p;
    int i;

    p.a = 0;
    p.b = 100;

    for (i = 0; i < 10; i++) {
        p.a = p.a + 1;
        p.b = p.b - 1;
    }

    printf("a %d b %d\n", p.a, p.b);
}
