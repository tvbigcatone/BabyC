/* IR must respect * over + (Claude regression) */
#include <stdio.h>
int main()
{
    int s = 0;
    int i;
    for (i = 1; i <= 4; i++)
        s = s + i * 2;
    printf("%d\n", s);
    /* more mixed ops */
    printf("%d\n", 1 + 2 * 3);
    printf("%d\n", (1 + 2) * 3);
    printf("%d\n", 10 - 3 * 2);
    printf("%d\n", 8 / 2 * 3);
    return 0;
}
