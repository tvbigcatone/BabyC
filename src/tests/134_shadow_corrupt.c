/* Bug #3: se TableSet fallisce dopo la manglatura del key, la tabella
 * resta corrotta. Shadowing ripetuto per forzare collisioni. */

#include <stdio.h>

void main()
{
    int n = 1;
    int i;
    int sum = 0;

    for (i = 0; i < 200; i++)
    {
        int n = 2;
        sum = sum + n;
        {
            int n = 3;
            sum = sum + n;
            {
                int n = 4;
                sum = sum + n;
            }
            sum = sum + n;
        }
        sum = sum + n;
    }

    /* sum = 200 * (2+3+4+3+2) = 200 * 14 = 2800 */
    printf("%d\n", sum);

    /* Variabili con nomi che hanno hash simile */
    {
        int abc = 10;
        int acb = 20;
        int bac = 30;
        int bca = 40;
        int cab = 50;
        int cba = 60;
        printf("%d %d %d %d %d %d\n", abc, acb, bac, bca, cab, cba);
    }

    printf("%d\n", n);
    printf("done\n");
}