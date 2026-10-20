#include <stdio.h>
/* W37: user functions returning double/float must keep FP semantics at the
 * call site (value used in printf, assignment, arithmetic, nested calls). */
double twice(double x) { return x * 2.0; }
double ident(double x) { return x; }
float  fl(float x) { return x + 1.0f; }
double noarg(void) { return 3.5; }
double scaled(int k) { return k + 0.25; }
int    toint(double x) { return (int)(x * 2.0); }
double chain(double x) { return twice(ident(x)) + noarg(); }
double fact(int n) { if (n <= 1) return 1.0; return n * fact(n - 1); }
int main(void)
{
    double t = twice(1.25);
    printf("t=%f\n", t);
    printf("ident=%f fl=%f noarg=%f scaled=%f\n", ident(4.5), fl(2.5f), noarg(), scaled(3));
    printf("toint=%d chain=%f\n", toint(1.75), chain(1.5));
    printf("fact=%f\n", fact(10));
    t = twice(twice(twice(0.5)));
    printf("t=%f sum=%f\n", t, twice(1.0) + twice(2.0));
    return 0;
}
