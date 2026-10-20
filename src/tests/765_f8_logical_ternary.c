/* F8: && || ternary lower under EXEC=both/vm */
#include <stdio.h>
int main(void)
{
    int a = 3, b = 0, c = 5;
    int x = (a && c) ? a + c : 0;
    int y = (b || a) ? 1 : 0;
    int z = (a && b) ? 9 : 7;
    printf("%d %d %d\n", x, y, z);
    return 0;
}
