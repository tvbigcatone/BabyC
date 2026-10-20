#include <stdio.h>

/* Anti-regression: same total as 272 but via while(); a plain scalar
 * while-loop with an int-base body must still run fast (Irb / native
 * tight-loop path), not fall back to the generic token interpreter. */

void main()
{
    int i;
    long long sum;

    i = 0;
    sum = 0;
    while (i < 1000000) {
        sum = sum + i;
        i++;
    }

    printf("%lld\n", sum);
}
