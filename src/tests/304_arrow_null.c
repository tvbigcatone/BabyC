#include <stdio.h>
struct box { int x; };
void main()
{
    struct box *p;
    int i;
    p = 0;
    for (i = 0; i < 3; i++)
        p->x = p->x + 1;
}
