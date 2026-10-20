#include <stdio.h>
#include <stdlib.h>

struct Node { int value; struct Node *next; };

int main(void)
{
    struct Node *a = (struct Node *)malloc(sizeof(struct Node));
    struct Node *b = (struct Node *)malloc(sizeof(struct Node));
    if (a == NULL || b == NULL) return 1;
    a->value = 11;
    a->next = b;
    b->value = 22;
    b->next = NULL;
    printf("%d %d\n", a->value, b->value);
    free(a);
    free(b);
    return 0;
}
