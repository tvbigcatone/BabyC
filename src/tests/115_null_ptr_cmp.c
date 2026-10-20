/* Confronti NULL/ptr in tutti i contesti: if, while, ternario, return. */

#include <stdio.h>
#include <stdlib.h>

struct Node { int v; };

void main()
{
    struct Node *p = NULL;
    struct Node *q = (struct Node *)malloc(sizeof(struct Node));
    int r;

    if (p == NULL)   printf("p null\n");
    if (q != NULL)   printf("q not null\n");
    if (NULL != p)   printf("BAD\n");
    else             printf("null != p ok\n");

    r = (p == NULL) ? 10 : 20;
    printf("%d\n", r);          /* 10 */

    r = (q != NULL) ? 30 : 40;
    printf("%d\n", r);          /* 30 */

    r = (p == NULL) ? ((q != NULL) ? 100 : 200) : 300;
    printf("%d\n", r);          /* 100 */

    free(q);
}