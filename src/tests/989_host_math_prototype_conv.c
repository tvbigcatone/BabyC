/* Host <math.h>/<ctype.h> calls: prototype conversion of int arguments to
 * double parameters (tree passed sqrt(k) raw int bits -> 0.000000) and
 * lowering of math/ctype host calls in linear. */
#include <stdio.h>
#include <math.h>
#include <ctype.h>
#include <stdlib.h>
int main(void)
{
    int k = 16; double d = 2.0;
    printf("%f %f %f\n", sqrt(k), pow(2, 10), fabs(-3));
    printf("%f %f\n", sqrt(d) * 2, floor(d + 0.7));
    double r = sqrt(k) + pow(d, 3.0);
    int t = (int)sqrt(k) + isalpha('a') * 0 + (isdigit('7') != 0) + toupper('b');
    printf("%f %d %c %d\n", r, t, tolower('Q'), isspace(' ') != 0);
    printf("%f %f\n", atof("2.5") * 2, fmod(7.5, 2));
    return 0;
}
