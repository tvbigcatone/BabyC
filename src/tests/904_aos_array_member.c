/* Anti-regression: aos[k].arr[i] under vm_strict (Index of Member of Index). */
#include <stdio.h>
struct Item { int id; int vals[3]; };
int main(void) {
    struct Item a[2];
    int i;
    for (i = 0; i < 2; i++) {
        a[i].id = i + 10;
        a[i].vals[0] = i * 3;
        a[i].vals[1] = i * 3 + 1;
        a[i].vals[2] = i * 3 + 2;
    }
    printf("%d %d %d\n", a[0].id, a[0].vals[0], a[0].vals[2]);
    printf("%d %d %d\n", a[1].id, a[1].vals[1], a[1].vals[2]);
    return 0;
}
