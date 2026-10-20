#include <stdio.h>
/* y is never written.  gcc -Wall warns; BabyC should say "uninitialized variable 'y'".
 * With BABYC_NO_IRB=1 (token path + old IR) nothing is reported in this loop shape. */
int main()
{
    int i, x, y;
    x = 1;
    for (i = 0; i < 5; i++)
    {
        if (i == 3) x = y + 1;
    }
    printf("%d\n", x);
    return 0;
}
