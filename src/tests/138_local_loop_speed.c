/* Bug #7: VariableScopeBegin/End fanno loop su tutta la hash table per
 * ogni blocco. Il test misura il tempo su un loop con molti locali. */

#include <stdio.h>

void main()
{
    int i;
    int sum = 0;

    for (i = 0; i < 5000; i++)
    {
        int a = i;
        int b = i + 1;
        int c = i + 2;
        int d = i + 3;
        sum = sum + a + b + c + d;
    }
    /* sum = 5000*(0+3) + 4 * sum(i) = 5000*3 + 4*12497500 = 15000 + 49990000 = 50005000 */
    printf("%d\n", sum);
}