/* Anti-regression: f(&a[i]) / f(&aos[i]) under vm_strict. */
#include <stdio.h>
typedef struct { int x; int y; int data[4]; } Node;
int take_int(int *p) { return *p; }
int score(Node *n) {
    int i, s = n->x + n->y;
    for (i = 0; i < 4; i = i + 1) s = s + n->data[i];
    return s;
}
int main(void) {
    int a[4];
    Node pool[2];
    int i, t;
    for (i = 0; i < 4; i = i + 1) a[i] = (i + 1) * 10;
    pool[0].x = 1; pool[0].y = 2;
    for (i = 0; i < 4; i = i + 1) pool[0].data[i] = i;
    pool[1].x = 3; pool[1].y = 4;
    for (i = 0; i < 4; i = i + 1) pool[1].data[i] = i + 10;
    t = take_int(&a[2]) + score(&pool[1]);
    printf("%d\n", t);
    return 0;
}
