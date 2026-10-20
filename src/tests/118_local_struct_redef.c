/* Two blocks both define struct S with different members.
 * Picoc approximates block-scope tags by redefining the type body. */

#include <stdio.h>

int main()
{
    {
        struct S {
            int x;
            int y;
        };
        struct S s;
        s.x = 1;
        s.y = 2;
        printf("%d\n", s.x + s.y);
    }
    {
        struct S {
            int a;
            int b;
            int c;
        };
        struct S s;
        s.a = 10;
        s.b = 20;
        s.c = 30;
        printf("%d\n", s.a + s.b + s.c);
    }
    return 0;
}
