#include <stdio.h>
struct B { unsigned a:4; unsigned b:12; };
int main(void)
{
    struct B s;
    s.a = 15;
    s.b = 4000;
    s.b >>= 1;
    printf("%u %u\n", s.a, s.b);
    return 0;
}
