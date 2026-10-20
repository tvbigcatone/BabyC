#include <stdio.h>
/* Host-call argument classification: pointer struct members, ternaries
 * with pointer arms (z ? z : "?") and ternaries whose arms are calls
 * returning int (must stay integers for %d). */
struct Q { char *s; int n; };
static int side = 0;
static int bump(int v) { side++; return v; }
static char *pick(int c) { return c ? "yes" : "no"; }
int main(void)
{
    struct Q c = { "lit", 3 };
    struct Q d;
    char *z = 0;
    char *w = "w";
    d.s = "x"; d.n = 1;
    printf("%s %d %s %d\n", c.s, c.n, d.s, d.n);
    printf("%s %s\n", z ? z : "?", w ? w : "?");
    printf("%d\n", 0 ? bump(99) : 1);
    printf("%d\n", 1 ? bump(100) : bump(999));
    printf("%d\n", side);
    printf("%s %s\n", pick(1), side > 5 ? pick(1) : pick(0));
    return 0;
}
