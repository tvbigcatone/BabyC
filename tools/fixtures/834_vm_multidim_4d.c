#include <stdio.h>
static unsigned short cube[2][2][2][3];
int main(void)
{
    int i, j, k, l;
    unsigned int value;
    i = 1;
    j = 0;
    k = 1;
    l = 2;
    cube[i][j][k][l] = 65535;
    value = cube[i][j][k][l];
    if (value != 65535) return 1;
    cube[0][1][0][1] = value - 1;
    value = cube[0][1][0][1];
    if (value != 65534) return 2;
    printf("%u %u\n", (unsigned int)cube[i][j][k][l], value);
    return 0;
}
