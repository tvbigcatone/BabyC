/* list walk: while (p != 0) { s += p->v; p = p->next; } */
#include <stdio.h>
struct Node { int v; struct Node *next; };
int main() {
    struct Node a, b, c;
    struct Node *p;
    int s;
    a.v = 1; b.v = 2; c.v = 3;
    a.next = &b; b.next = &c; c.next = 0;
    s = 0;
    p = &a;
    while (p != 0) {
        s = s + p->v;
        p = p->next;
    }
    printf("%d\n", s);
    return 0;
}
