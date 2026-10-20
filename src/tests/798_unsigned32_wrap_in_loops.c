#include <stdio.h>
unsigned int g;
unsigned int tab[16];
unsigned first(unsigned char *p) { return *p; }
int main(void) {
    unsigned int b = 0, c = 0, l = 837, i2 = 0; int i;
    unsigned char a[8]; unsigned char *q;
    g = 837u;
    for (i = 0; i < 3; i++) { b = (255u - l) / 2u; }
    printf("%u\n", b);
    for (i = 0; i < 3; i++) { b = (255u - g) / 2u; }
    printf("%u\n", b);
    for (i = 0; i < 3; i++) { b = (255u - g) % 1000u; c = (255u - g) >> 4; }
    printf("%u %u\n", b, c);
    for (i = 0; i < 3; i++) { b = ((255u - g) < 5u) ? 1u : 2u; }
    printf("%u\n", b);
    for (i = 0; i < 16; i++) tab[i] = (unsigned)i * 28291u ^ 1427628992u;
    for (i = 0; i < 29; i++) {
        i2 = (unsigned)i;
        c = ((tab[13u & 15] + (b << 14)) >> 8);
        b = ((i2 >> 16) | (255u - g)) / (((g / (g | 1u)) << 26) | 1u);
    }
    printf("%u %u %u\n", c, b, g);
    for (i = 0; i < 8; i++) a[i] = (unsigned char)(200 + i);
    q = a + 3;
    printf("%u %u %u %d\n", first(a + 3), *q, first(q + 2), (int)(q - a));
    return 0;
}
