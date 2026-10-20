/* break e continue dentro for annidati devono funzionare.
 * Bug #3: il ripristino di RunMode in ParseFor e' incompleto. */

#include <stdio.h>

void main()
{
    int i, j;
    int count = 0;

    for (i = 0; i < 5; i++)
    {
        for (j = 0; j < 5; j++)
        {
            if (j == 2)
                continue;
            if (i == 3)
                break;
            count++;
        }
        if (i == 3)
            break;
    }
    printf("%d\n", count);   /* atteso: 12 */

    i = 0;
    while (1)
    {
        if (i >= 10)
            break;
        i++;
    }
    printf("%d\n", i);       /* 10 */

    i = 0;
    count = 0;
    do
    {
        i++;
        if (i % 2 == 0)
            continue;
        count++;
    } while (i < 10);
    printf("%d %d\n", i, count);  /* 10 5 */
}