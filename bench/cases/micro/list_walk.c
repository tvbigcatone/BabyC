#include <stdio.h>
#include <stdlib.h>
typedef struct Node { int v; struct Node *next; } Node;
int main() {
    int i, k;
    long long s = 0;
    Node *head = 0, *p;
    for (i = 0; i < 2000; i++) {
        Node *n = (Node *)malloc(sizeof(Node));
        n->v = i;
        n->next = head;
        head = n;
    }
    for (k = 0; k < 300; k++) {
        for (p = head; p != 0; p = p->next)
            s += p->v;
    }
    printf("%lld\n", s);
    return 0;
}
