/* Regression: while(i+0<n) compound condition (not ident op bound) */
#include <stdio.h>
int main()
{
    int i = 0, s = 0;
    while (i + 0 < 5) {
        s = s + i;
        i = i + 1;
    }
    printf("%d\n", s);
    return 0;
}
