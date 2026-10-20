#include <stdio.h>
int main()
{
    /* 1+2 < 4  is (1+2)<4 not 1+(2<4) */
    printf("%d\n", 1 + 2 < 4);
    printf("%d\n", 1 + (2 < 4));
    printf("%d\n", 3 * 2 == 6);
    return 0;
}
