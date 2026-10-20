#include <stdio.h>
#include <stdlib.h>
struct Node { int value; struct Node *next; };
int id(int x) { return x; }
int dbl(int x) { return x + x; }
int add3(int a, int b, int c) { return a + b + c; }

struct Node *cons(int v, struct Node *tail) {
    struct Node *n = (struct Node *)malloc(sizeof(struct Node));
    n->value = v;
    n->next = tail;
    return n;
}

int sum_inc(struct Node *p) {
    int s = 0;
    while (p != NULL) {
        s = s + id(++p->value);
        p = p->next;
    }
    return s;
}

void walk_nested(struct Node *head) {
    struct Node *p;
    int acc = 0;
    int i;
    for (i = 0; i < 2; i++) {
        p = head;
        while (p != NULL) {
            acc = acc + dbl(++p->value);
            p = p->next;
        }
    }
    printf("%d\n", acc);
}

int multi_arg(struct Node *p) {
    int a, b, c;
    struct Node *n;
    if (p == NULL || p->next == NULL) return 0;
    n = p->next;
    a = ++p->value;
    b = --n->value;
    c = id(p->value);
    return add3(a, b, c);
}

void free_list(struct Node *p) {
    while (p != NULL) {
        struct Node *n = p->next;
        free(p);
        p = n;
    }
}

int main() {
    struct Node *list = cons(1, cons(2, cons(3, NULL)));
    printf("%d\n", sum_inc(list));
    printf("%d\n", list->value);
    printf("%d\n", list->next->value);
    printf("%d\n", list->next->next->value);
    printf("%d\n", multi_arg(list));
    {
        struct Node *l2 = cons(5, cons(6, NULL));
        walk_nested(l2);
        free_list(l2);
    }
    free_list(list);
    return 0;
}
