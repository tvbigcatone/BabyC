#include <stdio.h>
unsigned short step(unsigned short crc, unsigned char b) {
    int i;
    crc = crc ^ (unsigned short)b;
    for (i = 0; i < 8; i = i + 1) {
        if (crc & 1) {
            crc = crc >> 1;
            crc = crc ^ 0xA001;
        } else
            crc = crc >> 1;
    }
    return crc;
}
unsigned short run(int n) {
    unsigned short c = 0xFFFF;
    int i;
    for (i = 0; i < n; i = i + 1)
        c = step(c, (unsigned char)(i & 0xFF));
    return c;
}
int main() {
    printf("%u\n", (unsigned)run(400000));
    return 0;
}
