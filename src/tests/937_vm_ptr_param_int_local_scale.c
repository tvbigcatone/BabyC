#include <stdio.h>
#include <stdlib.h>
struct Node { int value; struct Node *next; };
int id(int x) { return x; }
struct Node *cons(int v, struct Node *tail) {
    struct Node *n = (struct Node *)malloc(sizeof(struct Node));
    n->value = v; n->next = tail; return n;
}
int sum_inc(struct Node *p) {
    int s = 0;
    while (p != NULL) {
        s = s + id(++p->value);
        p = p->next;
    }
    return s;
}
int main(void) {
    struct Node *list = cons(1, cons(2, cons(3, NULL)));
    printf("%d\n", sum_inc(list));
    printf("%d %d %d\n", list->value, list->next->value, list->next->next->value);
    return 0;
}
