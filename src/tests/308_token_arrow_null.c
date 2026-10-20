#include <stdio.h>
struct box { int x; };
void main()
{
    struct box *p = 0;
    p->x = 1;
}
