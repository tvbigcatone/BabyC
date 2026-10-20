/* Member accum loop: t += p->a (call+member hot path). */
#include <stdio.h>

typedef struct { int a, b; } S;

int main()
{
    S s;
    S *p;
    int i;
    long long t;

    s.a = 3;
    s.b = 4;
    p = &s;
    t = 0;
    for (i = 0; i < 5; i++)
        t = t + p->a + p->b;
    printf("%lld\n", t);
    return 0;
}
