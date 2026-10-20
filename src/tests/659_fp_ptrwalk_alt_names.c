#include <stdio.h>
#include <stdlib.h>
/* Same shape, different names/layout: payload first, link field "succ". */
typedef struct Cell {
    long payload;
    struct Cell *succ;
} Cell;
int main() {
    Cell *start = 0, *cur;
    long long total = 0;
    int k;
    for (k = 1; k <= 5; k++) {
        Cell *c = (Cell *)malloc(sizeof(Cell));
        c->payload = k * 10;
        c->succ = start;
        start = c;
    }
    for (cur = start; cur != 0; cur = cur->succ)
        total += cur->payload;
    printf("%lld\n", total);
    return 0;
}
