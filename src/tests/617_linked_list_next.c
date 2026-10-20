#include <stdio.h>
#include <stdlib.h>
struct Node { int value; struct Node *next; };
struct Node *cons(int v, struct Node *tail) {
    struct Node *n = (struct Node *)malloc(sizeof(struct Node));
    n->value = v; n->next = tail; return n;
}
int sum_list(struct Node *p) {
    int s = 0;
    while (p != NULL) { s += p->value; p = p->next; }
    return s;
}
void free_list(struct Node *p) {
    while (p != NULL) { struct Node *n = p->next; free(p); p = n; }
}
int main() {
    struct Node *list = cons(1, cons(2, cons(3, NULL)));
    printf("%d\n", sum_list(list));
    printf("%d\n", list->value);
    printf("%d\n", list->next->value);
    printf("%d\n", list->next->next->value);
    free_list(list);
    return 0;
}
