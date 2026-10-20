/* for/while bodies must run on BodyIr (list + accumulate) */
#include <stdio.h>
struct Node { int v; struct Node *next; };
int sum_list(struct Node *p) {
    int s;
    s = 0;
    while (p != 0) {
        s = s + p->v;
        p = p->next;
    }
    return s;
}
int sum_for(int n) {
    int i, s;
    s = 0;
    for (i = 0; i < n; i = i + 1)
        s = s + i;
    return s;
}
int main() {
    struct Node a, b, c;
    a.v = 1; b.v = 2; c.v = 3;
    a.next = &b; b.next = &c; c.next = 0;
    printf("%d\n", sum_list(&a));
    printf("%d\n", sum_for(10));
    return 0;
}
