#include <stdio.h>
#include <stdlib.h>

typedef struct { int tag; int key; } Item;

static int cmp_item_key(const void *a, const void *b)
{
    return ((const Item *)a)->key - ((const Item *)b)->key;
}

int main(void)
{
    Item items[4];
    int i;
    items[0].tag = 0; items[0].key = 9;
    items[1].tag = 1; items[1].key = 3;
    items[2].tag = 2; items[2].key = 7;
    items[3].tag = 3; items[3].key = 3;
    qsort(items, 4, sizeof(Item), cmp_item_key);
    for (i = 0; i < 4; ++i)
        printf("%d:%d ", items[i].key, items[i].tag);
    printf("\n");
    return 0;
}
