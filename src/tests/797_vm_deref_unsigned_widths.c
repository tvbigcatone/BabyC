#include <stdio.h>
unsigned char gb[6];
unsigned short gh[4];
unsigned int gw[3];
unsigned sum_b(unsigned char *p, int n) { unsigned s = 0; int i; for (i = 0; i < n; i++) s += *(p + i); return s; }
unsigned sum_h(unsigned short *p, int n) { unsigned s = 0; int i; for (i = 0; i < n; i++) s += *(p + i); return s; }
unsigned first_b(unsigned char *p) { return *p; }
int main(void) {
    unsigned s = 0; int i;
    unsigned char a[8]; unsigned short u[4]; unsigned int w[3]; short h[3];
    for (i = 0; i < 8; i++) a[i] = (unsigned char)(200 + i);
    for (i = 0; i < 4; i++) u[i] = (unsigned short)(60000 + i);
    for (i = 0; i < 3; i++) { w[i] = 4000000000u + (unsigned)i; h[i] = (short)(-1000 - i); }
    for (i = 0; i < 6; i++) gb[i] = (unsigned char)(250 + i);
    for (i = 0; i < 4; i++) gh[i] = (unsigned short)(65530 + i);
    for (i = 0; i < 3; i++) gw[i] = 3000000000u + (unsigned)i;
    for (i = 0; i < 8; i++) s += *(a + i);
    printf("%u\n", s); s = 0;
    for (i = 0; i < 4; i++) s += *(u + i);
    printf("%u\n", s); s = 0;
    for (i = 0; i < 3; i++) s += *(w + i) >> 4;
    printf("%u\n", s); s = 0;
    for (i = 0; i < 3; i++) s += *(h + i);
    printf("%u\n", s);
    printf("%u %u %u\n", sum_b(a, 8), sum_h(u, 4), first_b(a + 3));
    printf("%u %u %u\n", sum_b(gb, 6), sum_h(gh, 4), *(gw + 2) / 3u);
    printf("%u %u\n", (unsigned)*(a + 7), (unsigned)*(u + 3));
    return 0;
}
