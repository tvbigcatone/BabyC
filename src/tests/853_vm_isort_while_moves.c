#include <stdio.h>
/* BUG B (VM): insertion sort with a counter in the while body.
 * Expected "0 7 11".  Wrong result seen under BABYC_EXEC=vm: "3 6 0"
 * (array unsorted, while loop never executed): body_run_fail then tree fallback
 * on a partially modified state. */
int main(void)
{
    int a[8];
    int i, j, key;
    int moves = 0;
    for (i = 0; i < 8; i++)
        a[i] = (i * 5 + 3) % 8;
    for (i = 1; i < 8; i++)
    {
        key = a[i];
        j = i - 1;
        while (j >= 0 && a[j] > key)
        {
            a[j + 1] = a[j];
            j--;
            moves++;
        }
        a[j + 1] = key;
    }
    printf("%d %d %d\n", a[0], a[7], moves);
    return 0;
}
