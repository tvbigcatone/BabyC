#include <stdio.h>
#include <string.h>
/* CROSS HEAVY: . -> & * on struct array members + indices + loops */
typedef struct {
    char name[8];
    int vals[3];
} Item;

Item table[2];
Item *gp;

int accum(Item *it, int k)
{
    int *q;
    q = &it->vals[0];
    return it->vals[k] + *q + *(q + 1);
}

int main()
{
    Item local;
    int i, s;
    char *p;

    strcpy(table[0].name, "one");
    table[0].vals[0] = 1;
    table[0].vals[1] = 2;
    table[0].vals[2] = 3;

    strcpy(table[1].name, "two");
    for (i = 0; i < 3; i++)
        table[1].vals[i] = (i + 1) * 10;

    gp = &table[0];
    s = accum(gp, 2);           /* 3 + 1 + 2 = 6 */
    s = s + accum(&table[1], 1); /* 20 + 10 + 20 = 50 → s=56 */

    /* local copy via *gp style */
    local = table[1];
    s = s + local.vals[2];      /* +30 → 86 */

    p = table[0].name;
    printf("%s %s %d %d\n", p, gp->name, s, (*gp).vals[0]);
    return 0;
}
