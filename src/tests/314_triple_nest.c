#include <stdio.h>
struct A { int x; };
struct B { struct A a; };
struct C { struct B b; };
void main()
{
    struct C c;
    int i;
    c.b.a.x = 0;
    for (i = 0; i < 4; i++)
        c.b.a.x = c.b.a.x + 1;
    printf("x %d\n", c.b.a.x);
}
