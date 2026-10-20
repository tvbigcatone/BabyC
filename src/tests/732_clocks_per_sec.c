/* F3 regression: CLOCKS_PER_SEC must be positive (host-defined, typically 1e6).
 * Baseline bug: platform var could read as 0 → division by zero in timing code. */
#include <stdio.h>
#include <time.h>

int main()
{
    int cps = CLOCKS_PER_SEC;
    clock_t t0 = clock();
    int i, s = 0;
    for (i = 0; i < 1000; i++)
        s = s + i;
    clock_t t1 = clock();
    long elapsed_ms = (long)((t1 - t0) * 1000 / CLOCKS_PER_SEC);

    if (cps <= 0) {
        printf("FAIL cps=%d\n", cps);
        return 1;
    }
    if (elapsed_ms < 0) {
        printf("FAIL elapsed=%ld\n", elapsed_ms);
        return 1;
    }
    printf("cps=%d sum=%d OK\n", cps, s);
    return 0;
}
