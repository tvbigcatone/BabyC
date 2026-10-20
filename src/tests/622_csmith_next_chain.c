#include <stdio.h>
#include <stdlib.h>
struct N { int v; struct N *next; };
int neg(int x) { return -x; }
int sum2(int a, int b) { return a + b; }
struct N *mk(int v, struct N *t) {
    struct N *n = (struct N *)malloc(sizeof(struct N));
    n->v = v; n->next = t; return n;
}
int main() {
    struct N *p = mk(1, mk(2, mk(3, mk(4, NULL))));
    struct N *q;
    int s = 0;
    /* walk next->next while ++member as call arg */
    q = p;
    while (q != NULL) {
        s = s + neg(--q->v);
        if (q->next != NULL)
            s = s + sum2(++q->next->v, q->v);
        q = q->next;
    }
    printf("%d\n", s);
    printf("%d %d %d %d\n", p->v, p->next->v, p->next->next->v, p->next->next->next->v);
    while (p != NULL) { struct N *n = p->next; free(p); p = n; }
    return 0;
}
