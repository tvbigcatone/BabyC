/* Verifica che return dentro un ciclo for termini davvero la funzione.
 * Bug #3: ParseFor non ripristina correttamente RunModeReturn. */

#include <stdio.h>

int find_first(int *arr, int n, int target)
{
    int i;
    for (i = 0; i < n; i++)
    {
        if (arr[i] == target)
            return i;
    }
    return -1;
}

int nested(int n)
{
    int i, j;
    for (i = 0; i < n; i++)
    {
        for (j = 0; j < n; j++)
        {
            if (i * j == 6)
                return i * 100 + j;
        }
    }
    return -1;
}

void main()
{
    int a[5];
    a[0] = 10; a[1] = 20; a[2] = 30; a[3] = 40; a[4] = 50;

    printf("%d\n", find_first(a, 5, 30));   /* atteso: 2 */
    printf("%d\n", find_first(a, 5, 99));   /* atteso: -1 */

    printf("%d\n", nested(5));              /* atteso: 203  (i=2, j=3) */
}