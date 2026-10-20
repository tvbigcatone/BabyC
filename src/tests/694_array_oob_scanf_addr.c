/* Array OOB write with index >= size (no stdin — suite-safe).
 * Formerly used scanf("%d", &numbers[i]); which blocked without input
 * and is legal C for &a[N] one-past-end before the write. */
#include <stdio.h>
int main(void)
{
    int numbers[5];
    int i = 5;
    numbers[i] = 42;
    return 0;
}
