/* Regression: while(x) bare identifier condition */
#include <stdio.h>
int main()
{
    int x = 3, s = 0;
    while (x) {
        s = s + x;
        x = x - 1;
    }
    printf("%d\n", s);
    return 0;
}
