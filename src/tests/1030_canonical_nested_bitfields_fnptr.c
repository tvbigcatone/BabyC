#include <stdio.h>
struct Inner { signed int s:5; unsigned int u:5; };
struct Outer { struct Inner in; unsigned int tag:3; };
static unsigned char ret_uc(void) { return 250u; }
static unsigned short ret_us(void) { return 60000u; }
static unsigned int ret_u(void) { return 4000000000u; }
int main(void)
{
    struct Outer o = {{-7, 25u}, 5u};
    struct Outer *p = &o;
    unsigned char (*fuc)(void) = ret_uc;
    unsigned short (*fus)(void) = ret_us;
    unsigned int (*fu)(void) = ret_u;
    unsigned short a[3] = {60000u, 3u, 50000u};
    int x = -3;
    unsigned int u = 4u;
    printf("%d %d %d\n", p->in.s < 0, p->in.u > 16u, p->in.s < p->in.u);
    printf("%d %d %d\n", fuc() > 200u, fus() > 50000u, fu() > 3000000000u);
    printf("%d %d %d\n", a[0] > x, a[1] < u, a[2] > 40000u);
    printf("%u %u %u %u\n", (unsigned)fuc(), (unsigned)fus(), fu(), (unsigned)(x ? u : 2u));
    printf("%u %u\n", (unsigned)(x ? 1u : 2), (unsigned)(0 ? 3u : 4));
    return 0;
}
