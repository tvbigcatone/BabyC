#include <stdio.h>
struct pair { int a; int b; };
void main()
{
    struct pair s;
    struct pair *p;
    int i;
    s.a = 0; s.b = 0;
    p = &s;
    for (i = 0; i < 10; i++) {
        p->a = p->a + 1;
        p->b = p->b + 2;
    }
    printf("a %d b %d\n", p->a, p->b);
}
