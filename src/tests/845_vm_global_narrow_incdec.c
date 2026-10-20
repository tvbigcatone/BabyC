#include <stdio.h>
signed char a = 127;
unsigned char b = 255;
short c = 32767;
unsigned short d = 65535;
int main(void)
{
    int x, y, z, w;
    x = a++;
    y = ++b;
    z = c++;
    w = ++d;
    printf("%d %d %d %d\n", x + a, y + b, z + c, w + d);
    return 0;
}
