#include <stdio.h>
enum Small { ES_NEG = -3, ES_POS = 4 };
static int ret_int(void) { return -5; }
static unsigned int ret_uint(void) { return 6U; }
static unsigned long ret_ulong(void) { return 7UL; }
int main(void) {
    int i = -3; unsigned int u = 4U; unsigned char uc = 250U;
    unsigned short us = 65000U; enum Small e = ES_NEG;
    int a = (i < u); int b = ((int)uc > 200);
    int c = ((unsigned short)us > (int)uc);
    int d = ((i ? u : 2U) == 4U);
    int x = 1; int pre = ++x; int post = x++;
    unsigned int q = 1U; q <<= 3; q += 2U;
    int comma = (i, ret_int());
    unsigned long w = ret_ulong();
    printf("%d %d %d %d\n", a, b, c, d);
    printf("%d %d %d %u %d %lu\n", pre, post, x, q, comma, w);
    printf("%d %d %d %u\n", e < 0, ret_int() < ret_uint(), (unsigned int)ret_int() < ret_uint(), ret_uint() >> 1);
    return 0;
}
