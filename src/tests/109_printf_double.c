/* Stampa di double: PrintFP usa abs() invece di fabs() e chiama log10/pow.
 * Bug #2: output completamente sbagliato. */

#include <stdio.h>

void main()
{
    double a = 1.5;
    double b = -2.25;
    double c = 0.0;
    double d = 123.456;
    double e = 0.0001;

    printf("%f\n", a);
    printf("%f\n", b);
    printf("%f\n", c);
    printf("%f\n", d);
    printf("%f\n", e);
}