#include <stdio.h>
static unsigned char uc[1];
static unsigned short us[2];
static unsigned int ui[1];
static unsigned long long ull[1];
static short ss[1];
int main(void)
{
    uc[0] = 255;
    us[0] = 65535;
    ui[0] = 4294967295U;
    ull[0] = 18446744073709551615ULL;
    ss[0] = -32768;
    if (uc[0] != 255) return 1;
    if (us[0] != 65535) return 2;
    if (ui[0] != 4294967295U) return 3;
    if (ull[0] != 18446744073709551615ULL) return 4;
    if (ss[0] != -32768) return 5;
    us[1] = us[0];
    if (us[1] != 65535 || (int)us[1] != 65535) return 6;
    printf("%u %u %u %llu %d\n", (unsigned int)uc[0], (unsigned int)us[1],
        ui[0], ull[0], (int)ss[0]);
    return 0;
}
