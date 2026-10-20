#include <stdio.h>
#include <stdlib.h>
/* Structural ptr-walk: for (p=head; p!=0; p=p->next) s += p->v; */
typedef struct Node { int v; struct Node *next; } Node;
int main() {
    Node *head = 0, *p;
    long long s = 0;
    int i;
    for (i = 0; i < 10; i++) {
        Node *n = (Node *)malloc(sizeof(Node));
        n->v = i + 1;
        n->next = head;
        head = n;
    }
    for (p = head; p != 0; p = p->next)
        s += p->v;
    printf("%lld\n", s);
    return 0;
}
