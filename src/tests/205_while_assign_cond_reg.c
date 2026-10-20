/* Regression: assignment in while condition */
#include <stdio.h>
int main()
{
    int x = 3, s = 0;
    while ((x = x - 1) >= 0) {
        s = s + 1;
    }
    printf("%d %d\n", s, x);
    return 0;
}
