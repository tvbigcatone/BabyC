#include <stdio.h>

/* Regression: LICM must NOT hoist "sum = 0;" out of a for-body when
 * sum is rewritten later in the same iteration (sum += p). */
void main()
{
    int p, sum;
    for (p = 0; p < 5; p++) {
        sum = 0;
        sum += p;
        printf("pass=%d sum=%d\n", p, sum);
    }
}
