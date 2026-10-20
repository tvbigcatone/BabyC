#include <stdio.h>
struct S { int a; int b; };
struct S mk(void)
{
    struct S r;
    r.a = 7;
    r.b = 8;
    return r;
}
int main()
{
    struct S v[3];
    v[0].a = 1;
    v[0].b = 2;
    v[1] = mk();
    v[2].a = 5;
    v[2].b = 6;
    v[2].b++;
    printf("%d %d %d %d %d %d\n", v[0].a, v[0].b, v[1].a, v[1].b, v[2].a, v[2].b);
    return 0;
}
