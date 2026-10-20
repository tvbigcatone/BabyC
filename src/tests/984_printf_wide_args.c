#include <stdio.h>
/* Calls with more than 8 arguments (up to the 16 the host accepts) and
 * argument expressions that need many temporaries each. */
static int sq(int x) { return x * x; }
static int sum10(int a, int b, int c, int d, int e, int f, int g, int h, int i, int j)
{
    return a + b + c + d + e + f + g + h + i + j;
}
int main(void)
{
    int a = 3, b = 0, c = 7, d = -2;
    double x = 1.5;
    printf("%d %d %d %d %d %d %d %d %d %d %d %d\n",
           a ? 1 : 0, b ? 1 : 0, c > d ? sq(c) : -1, a == b ? 1 : 0,
           a != b ? a * c + d : 0, d < 0 ? -d : d, sq(a) + sq(c), a & c,
           a | d, c % a, (a + b) * (c - d), b ? sq(b) : sq(d));
    printf("%s %c %d %.2f %s %x %d %d %d %d %d %d %d %d %d\n",
           a ? "yes" : "no", 'Q', 42, x * 2, b ? "B" : "notB", 255,
           1, 2, 3, 4, 5, 6, 7, 8, 9);
    printf("%d\n", sum10(1, 2, 3, 4, 5, 6, 7, 8, 9, a ? 10 : 0));
    return 0;
}
