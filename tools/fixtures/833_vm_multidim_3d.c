#include <stdio.h>
static unsigned char cube[2][2][3];
int main(void)
{
    unsigned int a, b, c;
    cube[0][0][2] = 255;
    cube[1][0][1] = 128;
    cube[1][1][2] = cube[0][0][2] - 1;
    a = cube[0][0][2];
    b = cube[1][0][1];
    c = cube[1][1][2];
    if (a != 255) return 1;
    if (b != 128) return 2;
    if (c != 254) return 3;
    printf("%u %u %u\n", a, b, c);
    return 0;
}
