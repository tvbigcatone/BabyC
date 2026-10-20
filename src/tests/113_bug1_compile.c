/* Se heap.c ha FreeListBig = MemNode; (senza pc->), questo file
 * non arrivera' mai a essere eseguito. */

#include <stdio.h>
#include <stdlib.h>

void main()
{
    void *p = malloc(16);
    free(p);
    printf("heap ok\n");
}