#include <stdio.h>

struct Node {
    int v;
    struct Node *next;
};

struct Node a, b, c;

struct Node *head(void) { return &a; }
struct Node *second(void) { return head()->next; }

void link(void) {
    a.v = 1; b.v = 2; c.v = 3;
    a.next = &b;
    b.next = &c;
    c.next = 0;
}

int main() {
    struct Node *p;
    link();
    printf("%d\n", head()->v);
    printf("%d\n", second()->v);
    p = second();
    p->v = 20;
    printf("%d\n", b.v);
    head()->next->next->v = 30;
    printf("%d\n", c.v);
    /* walk via returns */
    p = head();
    while (p != 0) {
        printf("%d", p->v);
        if (p->next) printf(" ");
        p = p->next;
    }
    printf("\n");
    return 0;
}
