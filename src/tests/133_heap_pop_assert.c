/* Bug #2: HeapPopStack usa assert per validare Addr, disabilitato in NDEBUG. */

#include <stdio.h>
#include <stdlib.h>

int alloc_and_free(int n)
{
    int *p = (int *)malloc(sizeof(int) * n);
    int i;
    int sum = 0;
    for (i = 0; i < n; i++)
        p[i] = i;
    for (i = 0; i < n; i++)
        sum = sum + p[i];
    free(p);
    return sum;
}

void main()
{
    int i;
    int total = 0;
    for (i = 0; i < 500; i++)
        total = total + alloc_and_free(10);
    printf("%d\n", total);
}