#include <stdio.h>
static signed char s8[2];
static unsigned char u8[2];
static short s16[2];
static unsigned short u16[2];
int main(void)
{
    s8[0] = -128; u8[0] = 255;
    s16[0] = -32768; u16[0] = 65535;
    s8[1] = s8[0] + 1; u8[1] = u8[0] + 1;
    s16[1] = s16[0] + 1; u16[1] = u16[0] + 1;
    if (s8[1] != -127 || u8[1] != 0) return 1;
    if (s16[1] != -32767 || u16[1] != 0) return 2;
    printf("%d %u %d %u\n", s8[1], u8[1], s16[1], u16[1]);
    return 0;
}
