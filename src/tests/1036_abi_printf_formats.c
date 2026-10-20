#include <stdio.h>
int main(void) {
    int d = -17; long l = -123456L; long long ll = -1234567890123LL;
    unsigned int u = 4000000000U; unsigned long ul = 4000000000UL;
    unsigned int x = 0x12abU; double f = 3.25; const char *s = "abi";
    printf("%d %ld %lld %u %lu %x %.2f %lf %s\n", d, l, ll, u, ul, x, f, f, s);
    return 0;
}
