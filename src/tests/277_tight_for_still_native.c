#include <stdio.h>

/* Anti-regression: contrasts with the 267-271 reject cases above. A plain
 * scalar for-loop (int index, long long accumulator, both int-base) must
 * still engage Irb / the native tight-loop path, not fall back to the
 * generic token interpreter. Use long long so the total (> INT32_MAX) is
 * portable across LP64 (Unix) and LLP64 (Windows) ABIs. */

void main()
{
    int i;
    long long sum;

    sum = 0;
    for (i = 0; i < 1000000; i++)
        sum = sum + i;

    printf("%lld\n", sum);
}
