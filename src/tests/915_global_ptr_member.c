/* Anti-regression: global T* gp->m / gp->name / gp->vals[i] under vm_strict. */
#include <stdio.h>
#include <string.h>
typedef struct { char name[8]; int vals[3]; } Item;
Item table[2];
Item *gp;
int main(void) {
    strcpy(table[0].name, "ab");
    table[0].vals[0] = 3;
    table[0].vals[1] = 4;
    gp = &table[0];
    printf("%s %d %d\n", gp->name, gp->vals[0], (*gp).vals[1]);
    return 0;
}
