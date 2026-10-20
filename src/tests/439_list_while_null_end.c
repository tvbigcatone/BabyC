/* list walk while(p) ending at NULL */
#include <stdio.h>
struct Node { int v; struct Node *next; };
int main() {
    struct Node a, b;
    struct Node *p;
    int s;
    a.v = 4; b.v = 5;
    a.next = &b; b.next = 0;
    s = 0;
    p = &a;
    while (p) {
        s = s + p->v;
        p = p->next;
    }
    printf("%d\n", s);
    return 0;
}
