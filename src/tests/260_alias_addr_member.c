#include <stdio.h>
#include <string.h>
struct S { int a; int b; };
int main()
{
    struct S s;
    struct S z;
    struct S v[2];
    int *p;
    int *q;
    p = &s.b;
    *p = 5;
    memset(&z, 0, sizeof(z));
    q = &v[1].a;
    *q = 9;
    printf("%d %d %d %d\n", s.b, z.a, z.b, v[1].a);
    return 0;
}
