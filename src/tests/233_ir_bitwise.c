#include <stdio.h>
int main()
{
    int a = 0xF0;
    int b = 0x0F;
    printf("%d %d %d\n", a & b, a | b, a ^ b);
    printf("%d %d\n", 1 << 4, 128 >> 3);
    printf("%d\n", ~0 & 1);
    return 0;
}
