#include <stdio.h>
#include <string.h>
/* CROSS: FormatPure sprintf + global struct array member + loop + via-pointer
 * Overlaps: g_m.ita (struct member fix), sprintf cache, index write, p->ita
 */
typedef struct {
    int id;
    char ita[32];
} Message;
Message g_m;

void fill_via_sprintf(int n)
{
    sprintf(g_m.ita, "id=%d", n);   /* global struct array field as dest */
}

int main()
{
    Message *p;
    int i;
    char local[32];

    /* isolated-style first write on global member */
    g_m.id = 1;
    sprintf(g_m.ita, "id=%d", 10);
    printf("%s\n", g_m.ita);

    /* cache hit on same key into different buffer */
    sprintf(local, "id=%d", 10);
    printf("%s\n", local);

    /* loop: many hits + occasional miss */
    for (i = 0; i < 5; i++)
        fill_via_sprintf(10);       /* hits */
    fill_via_sprintf(11);           /* miss */
    printf("%s\n", g_m.ita);

    /* via pointer still sees last write */
    p = &g_m;
    printf("%d %s\n", p->id, p->ita);

    /* index write still works after sprintf */
    p->ita[0] = 'X';
    printf("%s\n", g_m.ita);
    return 0;
}
