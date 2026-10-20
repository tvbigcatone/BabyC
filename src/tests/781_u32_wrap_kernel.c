/* Anti-regression W24: unsigned 32-bit wrap in kernel loop
 * (255u - g)/2u with g>255 must truncate to 32-bit before /.
 * Without IrApplyBinOpFlags Unsigned&&!Wide branch, kernel yields 4294967005. */
#include <stdio.h>
unsigned g = 837;
int main(void)
{
    unsigned b = 0;
    int i;
    for (i = 0; i < 5; i++)
        b = (255u - g) / 2u;
    printf("%u\n", b);
    unsigned x = (0u - 1u);
    printf("%u %u %u\n", x / 2u, x % 3u, x >> 1);
    return 0;
}
