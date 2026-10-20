#include <stdio.h>

/* Sibling for-loops that declare the same loop variable name must each use
 * their own variable. The first loop's condition used to resolve `i` by name,
 * newest-first, hitting the second (still uninitialised) `i` -> "'i' is
 * undefined" when run through the VM (BABYC_EXEC=vm). */
#define N 100
static char s[N + 1];
static int g[16];

int main(void)
{
    int c = 0;
    long t = 0;

    for (int i = 2; i <= N; i++)
        s[i] = 1;
    for (int i = 2; i * i <= N; i++)
        if (s[i])
            for (int j = i * i; j <= N; j += i)
                s[j] = 0;
    for (int i = 2; i <= N; i++)
        c += s[i];
    printf("primes<=%d: %d\n", N, c);

    for (int i = 0; i < 16; i++)
        g[i] = i * 3;
    for (int i = 0; i < 16; i++)
        t += g[i];
    printf("sum=%ld\n", t);
    return 0;
}
