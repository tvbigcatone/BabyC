#include <stdio.h>
int main(void)
{
    signed char c = 126;
    unsigned char u = 254;
    short s = 32766;
    unsigned short us = 65534;
    int a, b, d, e, f, g, h, i;
    int ca, ua, sa, usa;

    a = c++;
    ca = c;
    b = ++c;
    d = u++;
    ua = u;
    e = ++u;
    f = s++;
    sa = s;
    g = ++s;
    h = us++;
    usa = us;
    i = ++us;

    printf("%d %d %d %d\n", a, ca, b, (int)c);
    printf("%d %d %d %d\n", d, ua, e, (int)u);
    printf("%d %d %d %d\n", f, sa, g, (int)s);
    printf("%d %d %d %d\n", h, usa, i, (int)us);
    return 0;
}
