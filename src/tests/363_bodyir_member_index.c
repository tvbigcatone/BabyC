#include <stdio.h>
/* Anti-regression: Body IR p->arr[i] must resolve member address (not load).
 * Also OR-chain of char-array members via pointer param (probe p1). */
typedef struct {
    char a[8];
    char b[8];
    char c[8];
} Msg;

int first(Msg *m)
{
    return m->a[0];
}

int or_chain(Msg *m)
{
    return (m->a[0] || m->b[0] || m->c[0]);
}

int sequential(Msg *m)
{
    int r = 0;
    if (m->a[0] != '\0') r = 1;
    if (m->b[0] != '\0') r = 1;
    if (m->c[0] != '\0') r = 1;
    return r;
}

int main()
{
    Msg g;
    int i;
    for (i = 0; i < 8; i++) {
        g.a[i] = 0;
        g.b[i] = 0;
        g.c[i] = 0;
    }
    printf("%d %d %d\n", first(&g), or_chain(&g), sequential(&g));
    g.a[0] = 'A';
    printf("%d %d %d %c\n", first(&g), or_chain(&g), sequential(&g), (char)first(&g));
    g.a[0] = 0;
    g.b[0] = 'B';
    printf("%d %d\n", or_chain(&g), sequential(&g));
    return 0;
}
