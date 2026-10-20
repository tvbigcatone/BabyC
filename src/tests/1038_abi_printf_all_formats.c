#include <stdio.h>
int main(void) {
    int d = 23; long l = 123456L; long long ll = 123456789012LL;
    unsigned int u = 345U; unsigned long ul = 456789UL;
    unsigned int x = 0x5acU; double f = 6.125; const char *s = "format-ok";
    printf("%d %ld %lld %u %lu %x %.3f %lf %s\n", d, l, ll, u, ul, x, f, f, s);
    return 0;
}
