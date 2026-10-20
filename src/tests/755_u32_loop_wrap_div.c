#include <stdio.h>
/* unsigned wrap (3u-i)/5u in for-loop — kernel path must use unsigned arith */
int main(void)
{
    unsigned int a = 0, i2;
    int i;
    for (i = 0; i < 21; i++) {
        i2 = (unsigned int)i;
        a = ((3u) - (i2)) / 5u;
    }
    printf("%u\n", a);
    return 0;
}
