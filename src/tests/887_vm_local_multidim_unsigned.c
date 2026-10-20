#include <stdio.h>
int main(void)
{
    unsigned char uc[2][3];
    unsigned short us[2][3][2];
    unsigned int ui[2][2];
    unsigned long long ull[2][2];
    signed short ss[2][2];
    uc[1][2] = 255;
    us[1][2][1] = 65535;
    ui[1][1] = 4294967295U;
    ull[1][1] = 18446744073709551615ULL;
    ss[1][1] = -32768;
    printf("%u %u %u %llu %d\n",
           uc[1][2], us[1][2][1], ui[1][1], ull[1][1], ss[1][1]);
    return 0;
}
