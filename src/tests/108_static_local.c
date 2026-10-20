/* Una variabile static locale deve mantenere il valore tra le chiamate. */

#include <stdio.h>

int counter()
{
    static int c = 0;
    c = c + 1;
    return c;
}

void main()
{
    printf("%d\n", counter());   /* 1 */
    printf("%d\n", counter());   /* 2 */
    printf("%d\n", counter());   /* 3 */
}