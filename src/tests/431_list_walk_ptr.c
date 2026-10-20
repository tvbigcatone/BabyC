#include <stdio.h>
struct Node { int v; struct Node *next; };
int main() {
    struct Node a, b, c;
    struct Node *p;
    int s;
    a.v = 1; b.v = 2; c.v = 3;
    a.next = &b; b.next = &c; c.next = 0;
    p = &a;
    s = p->v;
    p = p->next;
    s = s + p->v;
    p = p->next;
    s = s + p->v;
    printf("%d\n", s);
    return 0;
}
