#include <stdio.h>
int main()
{
    int x = 2147483647; /* INT_MAX */
    int y = x + 1;
    printf("%d\n", y < 0 ? 1 : 0);
    printf("%d\n", (int)300);
    return 0;
}
