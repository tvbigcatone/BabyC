#include <stdio.h>
/* Brace-init of struct locals: pointer members (literal and variable),
 * non-constant scalars of every width, nested aggregates and designators.
 * Pointer members were silently left NULL by the shared brace pack. */
struct P { const char *s; int n; long l; short h; char c; };
struct In { int a; int b; int c; };
struct Out { struct In x; int y; const char *tag; };
int main(void)
{
    const char *q = "hey";
    int k = 3, i, t = 0;
    struct P a = { q, k * 2, k + 100000L, (short)(k - 7), 'A' + k };
    struct P b = { "lit", 5, 6, 7, 'z' };
    struct P d = { .n = k, .s = q };
    printf("%s %d %ld %d %c\n", a.s, a.n, a.l, a.h, a.c);
    printf("%s %d %ld %d %c\n", b.s, b.n, b.l, b.h, b.c);
    printf("%s %d %ld\n", d.s, d.n, d.l);
    for (i = 0; i < 3; i++) {
        struct Out o = { { i, i + 1, i + 2 }, i + 10, i ? q : "zero" };
        t += o.x.a + o.x.b + o.x.c + o.y;
        printf("%s\n", o.tag);
    }
    printf("%d\n", t);
    return 0;
}
