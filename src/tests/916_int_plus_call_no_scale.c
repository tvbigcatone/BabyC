/* Anti-regression: int s = f(); s = s + g(); must not scale as pointer. */
#include <stdio.h>
typedef struct { char name[8]; int vals[3]; } Item;
Item table[2];
int accum(Item *it, int k) {
    int *q = &it->vals[0];
    return it->vals[k] + *q + *(q + 1);
}
int main(void) {
    int s;
    table[0].vals[0] = 1; table[0].vals[1] = 2; table[0].vals[2] = 3;
    table[1].vals[0] = 10; table[1].vals[1] = 20; table[1].vals[2] = 30;
    s = accum(&table[0], 2);
    s = s + accum(&table[1], 1);
    printf("%d\n", s);
    return 0;
}
