#include <stdio.h>

struct Node {
    int key;
    int val;
    struct Node *next;
};

struct Node *list_push(struct Node *head, struct Node *n) {
    n->next = head;
    return n;
}

int list_sum(struct Node *p) {
    int s;
    s = 0;
    while (p != 0) {
        s = s + p->val;
        p = p->next;
    }
    return s;
}

int list_find(struct Node *p, int key) {
    while (p != 0) {
        if (p->key == key)
            return p->val;
        p = p->next;
    }
    return -1;
}

struct Node *list_reverse(struct Node *head) {
    struct Node *prev;
    struct Node *cur;
    struct Node *nx;
    prev = 0;
    cur = head;
    while (cur != 0) {
        nx = cur->next;
        cur->next = prev;
        prev = cur;
        cur = nx;
    }
    return prev;
}

void list_foreach_add(struct Node *p, int delta) {
    while (p != 0) {
        p->val = p->val + delta;
        p = p->next;
    }
}

int main() {
    struct Node a, b, c, d;
    struct Node *h;
    int i;
    a.key = 1; a.val = 10;
    b.key = 2; b.val = 20;
    c.key = 3; c.val = 30;
    d.key = 4; d.val = 40;
    h = 0;
    h = list_push(h, &a);
    h = list_push(h, &b);
    h = list_push(h, &c);
    h = list_push(h, &d);
    printf("%d\n", list_sum(h));
    printf("%d\n", list_find(h, 3));
    printf("%d\n", list_find(h, 9));
    h = list_reverse(h);
    printf("%d\n", list_find(h, 1));
    list_foreach_add(h, 5);
    printf("%d\n", list_sum(h));
    return 0;
}
