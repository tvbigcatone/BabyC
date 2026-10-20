/* Bug #4: IgnorePrecedence non resettato dopo '?:'. */

#include <stdio.h>

int side = 0;

int bump()
{
    side++;
    return side;
}

void main()
{
    int a = 0;
    int b = 0;
    int c = 0;
    int r;

    r = (1 ? 10 : bump());
    printf("%d %d\n", r, side);           /* 10 0 */

    r = (0 ? bump() : 20);
    printf("%d %d\n", r, side);           /* 20 0 */

    r = 1 ? 10 : 20 + 5;
    printf("%d\n", r);                    /* 10 */

    r = 0 ? 10 : 20 + 5;
    printf("%d\n", r);                    /* 25 */

    r = 1 ? (0 ? 100 : 200) : 300;
    printf("%d\n", r);                    /* 200 */

    a = 0; b = 0; c = 0;
    r = (a ? b++ : c++);
    printf("%d %d %d\n", r, b, c);        /* 0 0 1 */

    a = 1; b = 0; c = 0;
    r = (a ? b++ : c++);
    printf("%d %d %d\n", r, b, c);        /* 0 1 0 */

    /* Operatori dopo ':' devono essere eseguiti */
    r = 0 ? 1 : 2 + 3 * 4;
    printf("%d\n", r);                    /* 14 */
}