#include <stdio.h>
struct Node { int v; struct Node *next; };
int main() {
    struct Node a, b;
    struct Node *p;
    int s, n;
    a.v = 7; b.v = 8;
    a.next = &b; b.next = 0;
    p = &a;
    s = 0; n = 0;
    do {
        s = s + p->v;
        p = p->next;
        n = n + 1;
    } while (p != 0 && n < 5);
    printf("%d\n", s);
    return 0;
}
