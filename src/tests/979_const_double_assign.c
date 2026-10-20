#include <stdio.h>
/* const double local: the FP declaration path must record const. */
int main() {
    const double d = 1.5;
    d = 2.0;
    printf("x %f\n", d);
    return 0;
}
