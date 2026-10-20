#include <stdio.h>
static unsigned short g;
int main(void)
{
    unsigned short x;
    g = 65535;
    x = 65535;
    if (g != 65535 || x != 65535) return 1;
    if ((int)g != 65535 || (int)x != 65535) return 2;
    printf("%u %u\n", (unsigned int)g, (unsigned int)x);
    return 0;
}
