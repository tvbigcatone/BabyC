#include <stdio.h>
#include <stdlib.h>

int main()
{
    /* free(NULL) deve essere no-op, non deve crashare */
    void *p = NULL;
    free(p);
    printf("free_null=ok\n");

    /* doppio free di malloc(0): molti allocatori restituiscono un puntatore
     * valido; free(NULL) dopo deve restare sicuro */
    void *q = malloc(0);
    if (q != NULL) {
        free(q);
    }
    printf("malloc0=ok\n");

    /* free(NULL) ripetuto */
    free(NULL);
    free(NULL);
    printf("free_null_twice=ok\n");

    return 0;
}