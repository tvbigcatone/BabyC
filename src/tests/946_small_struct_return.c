#include <stdio.h>

/* Structs of 1..4 bytes returned by value. In the linear VM the callee's
 * owned blob was returned as a dangling pointer when sizeof <= 4, so the
 * caller printed garbage (BABYC_EXEC=vm). Includes a bitfield struct. */
struct P2 { short a; short b; };
struct P1 { char a; char b; };
struct B  { int a:5; int b:3; };

struct P2 mk2(int x, int y) { struct P2 s; s.a = x; s.b = y; return s; }
struct P1 mk1(int x, int y) { struct P1 s; s.a = x; s.b = y; return s; }
struct B  mkb(int x, int y) { struct B s; s.a = x; s.b = y; return s; }

int main(void)
{
    struct P2 t;
    struct P1 c;
    struct B  b;
    t = mk2(10, 3);
    printf("%d %d\n", t.a, t.b);
    struct P2 u = mk2(7, 8);
    printf("%d %d\n", u.a, u.b);
    c = mk1(5, 6);
    printf("%d %d\n", c.a, c.b);
    b = mkb(10, 3);
    printf("%d %d\n", b.a, b.b);
    struct B d = mkb(-7, -2);
    printf("%d %d\n", d.a, d.b);
    return 0;
}
