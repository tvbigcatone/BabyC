/* Shadowing in tre scope annidati + shadowing dentro un loop.
 * Bug #7: la manglatura del key con bit 1 puo' corrompere la tabella
 * quando ci sono piu' di due livelli. */

#include <stdio.h>

void main()
{
    int n = 1;
    int i;

    printf("%d\n", n);           /* 1 */
    {
        int n = 2;
        printf("%d\n", n);       /* 2 */
        {
            int n = 3;
            printf("%d\n", n);   /* 3 */
            {
                int n = 4;
                printf("%d\n", n); /* 4 */
            }
            printf("%d\n", n);   /* 3 */
        }
        printf("%d\n", n);       /* 2 */
    }
    printf("%d\n", n);           /* 1 */

    /* Shadowing dentro un loop: la variabile interna deve rinascere
     * ad ogni iterazione con il valore dell'inizializzatore. */
    for (i = 0; i < 3; i++)
    {
        int n = i * 10;
        printf("%d\n", n);
    }
    /* n esterna deve essere ancora 1 */
    printf("%d\n", n);           /* 1 */

    /* Ombra una variabile in un blocco annidato in un loop */
    for (i = 0; i < 2; i++)
    {
        int x = 100 + i;
        {
            int x = 200 + i;
            printf("%d\n", x);   /* 200, 201 */
        }
        printf("%d\n", x);       /* 100, 101 */
    }
}