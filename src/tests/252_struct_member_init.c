#include <stdio.h>
struct S { int a; int b; };
int main()
{
    struct S s;
    struct S t;
    int x;
    s.a = 1;
    s.b = 2;
    t = s;
    x = 5;
    printf("%d %d %d %d\n", s.a, s.b, t.a, t.b);
    printf("%d %d %d\n", (int)sizeof(x), (int)sizeof(s.b), (int)sizeof(s));
    return 0;
}
