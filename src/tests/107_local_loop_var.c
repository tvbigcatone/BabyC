/* Un loop che dichiara una variabile locale molte volte deve riutilizzare
 * lo storage, non allocare ogni volta.
 * Bug #7: senza il riuso delle entry OutOfScope, il loop esaurisce la memoria. */

#include <stdio.h>

void main()
{
    int i;
    int sum = 0;

    for (i = 0; i < 10000; i++)
    {
        int local = i;
        sum = sum + local;
    }
    printf("%d\n", sum);   /* atteso: 49995000 */
}