#include <stdio.h>

/* Mixed-width integer arithmetic must not truncate the expression value to int.
 *
 * HANDOFF_DELTA §7.2: the token path used to ExpressionPushInt() after long/int
 * (and long long/int) ops, typing the result as int and truncating to 32-bit.
 *
 * We use long long here so the same .expect is valid on LP64 (Unix, long=64)
 * and LLP64 (Windows, long=32). 3000000000 does not fit in a 32-bit signed long.
 */
void main()
{
    long long x, r1, r2, r3, r4;
    int y, z;
    short sh;
    char ch;

    x = 3000000000LL;
    y = 1000000;
    z = 31;
    sh = 5;
    ch = 3;

    r1 = x + y;
    r2 = x * z;
    r3 = x + sh;
    r4 = x * ch;
    printf("x+int %lld\n", r1);
    printf("x*int %lld\n", r2);
    printf("x+short %lld\n", r3);
    printf("x*char %lld\n", r4);

    r1 = x;
    r1 = r1 * 31 + y;
    printf("(x*31)+y %lld\n", r1);

    r2 = (x += y);
    printf("x+=y %lld\n", r2);

    /* long + int with values safe for 32-bit long (LLP64). Basic smoke check. */
    {
        long a = 1000000000L;
        int b = 7;
        long s = a + b;
        printf("long+int %ld\n", s);
    }
}
