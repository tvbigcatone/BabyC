/* Verifica cast tra char**, void**, int** e accesso a struct via void*. */

#include <stdio.h>
#include <stdlib.h>

struct Node {
    int value;
    struct Node *next;
};

void main()
{
    struct Node *a = (struct Node *)malloc(sizeof(struct Node));
    struct Node *b = (struct Node *)malloc(sizeof(struct Node));
    void *raw;
    struct Node **pp;
    struct Node *again;

    a->value = 1;
    a->next = b;
    b->value = 2;
    b->next = NULL;

    raw = a;
    again = raw;
    printf("%d\n", again->value);        /* 1 */

    pp = &a;
    printf("%d\n", (*pp)->value);        /* 1 */
    printf("%d\n", (*pp)->next->value);  /* 2 */

    *pp = b;
    printf("%d\n", a->value);            /* 2 */
    printf("%d\n", (*pp)->value);        /* 2 */

    /* *pp = b aliases a to b — free the original node only once */
    free(again);
    free(b);
}