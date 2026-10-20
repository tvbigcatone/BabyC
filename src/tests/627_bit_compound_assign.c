#include <stdio.h>

/* Body-IR must accept ^= >>= &= |= <<= so bit-twiddling loops stay on IR path. */
unsigned short step(unsigned short crc, unsigned char b)
{
    int i;
    crc ^= (unsigned short)b;
    for (i = 0; i < 8; i++) {
        if (crc & 1) {
            crc >>= 1;
            crc ^= 0xA001;
        } else {
            crc >>= 1;
        }
    }
    return crc;
}

void main()
{
    unsigned short c = 0xFFFF;
    unsigned int x = 0xFF;
    c = step(c, 0xAB);
    printf("crc=%u\n", (unsigned int)c);
    x &= 0x0F;
    x |= 0xA0;
    x <<= 4;
    printf("x=%u\n", x);
}
