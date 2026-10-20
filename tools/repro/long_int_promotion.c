#include <stdio.h>
int main()
{
    long x, r1, r2, r3, r4;
    int y, z;
    short sh;
    char ch;
    x = 3000000000;
    y = 1000000;
    z = 31;
    sh = 5;
    ch = 3;
    r1 = x + y;
    r2 = x * z;
    r3 = x + sh;
    r4 = x * ch;
    printf("x+int   %ld\nx*int   %ld\nx+short %ld\nx*char  %ld\n", r1, r2, r3, r4);
    r1 = x; r1 = r1 * 31 + y;
    printf("(x*31)+y %ld\n", r1);
    return 0;
}
