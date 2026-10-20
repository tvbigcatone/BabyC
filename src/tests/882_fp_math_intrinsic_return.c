#include <stdio.h>
#include <math.h>
#include <stdlib.h>
/* W37: intrinsic (library) calls returning double were truncated to integer
 * in the IR path (sqrt(2.0) printed 0.000000). */
double hyp(double a, double b) { return sqrt(a * a + b * b); }
int main(void)
{
    double x = 2.0;
    double s = sqrt(x);
    printf("s=%f\n", s);
    printf("fabs=%f direct=%f\n", fabs(-2.5), sqrt(9.0));
    printf("pow=%f floor=%f\n", pow(2.0, 10.0), floor(2.7));
    printf("sin=%f cos=%f exp=%f\n", sin(1.0), cos(1.0), exp(1.0));
    printf("i=%d hyp=%f atof=%f\n", (int)sqrt(16.0), hyp(3.0, 4.0), atof("3.25"));
    printf("mix=%f\n", sqrt(2.0) * 7 + fabs(-1.5));
    return 0;
}
