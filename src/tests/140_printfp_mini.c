/* Bug #9: PrintFP usa log10/pow, ma in BUILTIN_MINI_STDLIB <math.h>
 * potrebbe non essere disponibile.
 * Il test esercita la stampa di double in tutte le forme. */

#include <stdio.h>

void main()
{
    double a = 1.5;
    double b = -2.25;
    double c = 0.0;
    double d = 123.456;
    double e = 0.0001;
    double f = 1e8;
    double g = -1e-8;

    printf("%f\n", a);
    printf("%f\n", b);
    printf("%f\n", c);
    printf("%f\n", d);
    printf("%f\n", e);
    printf("%f\n", f);
    printf("%f\n", g);
}