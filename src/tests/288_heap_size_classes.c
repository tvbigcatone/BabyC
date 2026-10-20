/* Anti-regression: many small/medium allocations + frees (exercises extended buckets) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define N 1500
void *ptrs[N];
int main()
{
    int i, fails = 0;
    for (i = 0; i < N; i++) {
        int sz = 8 + (i % 120);
        ptrs[i] = malloc(sz);
        if (!ptrs[i]) { fails++; break; }
        memset(ptrs[i], 0x5a, sz);
        if (((unsigned char *)ptrs[i])[0] != 0x5a) fails++;
    }
    for (i = 0; i < N; i += 2) {
        if (ptrs[i]) { free(ptrs[i]); ptrs[i] = NULL; }
    }
    for (i = 0; i < N; i += 2) {
        int sz = 12 + (i % 110);
        ptrs[i] = malloc(sz);
        if (!ptrs[i]) fails++;
        else memset(ptrs[i], 0xc3, sz);
    }
    for (i = 0; i < N; i++)
        if (ptrs[i]) free(ptrs[i]);
    printf(fails ? "FAIL %d\n" : "ok\n", fails);
    return fails ? 1 : 0;
}
