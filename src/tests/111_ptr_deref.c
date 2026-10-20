/* Allocazione/free ripetuti per stressare il freelist.
 * Bug #1: FreeListBig = MemNode (senza pc->) corrompe la lista.
 * Bug #9: padding non inizializzato. */

#include <stdio.h>
#include <stdlib.h>

void main()
{
    int i;
    int *ptrs[100];

    for (i = 0; i < 100; i++)
        ptrs[i] = (int *)malloc(sizeof(int));

    for (i = 0; i < 100; i++)
        *ptrs[i] = i;

    for (i = 0; i < 100; i++)
        if (*ptrs[i] != i)
            printf("CORRUPTION at %d: %d\n", i, *ptrs[i]);

    for (i = 0; i < 100; i++)
        free(ptrs[i]);

    for (i = 0; i < 50; i++)
        ptrs[i] = (int *)malloc(sizeof(int) * 10);

    for (i = 0; i < 50; i++)
    {
        ptrs[i][0] = i;
        ptrs[i][9] = i * 2;
    }

    for (i = 0; i < 50; i++)
    {
        if (ptrs[i][0] != i || ptrs[i][9] != i * 2)
            printf("CORRUPTION2 at %d\n", i);
    }

    for (i = 0; i < 50; i++)
        free(ptrs[i]);

    printf("done\n");
}