/* Anti-regression: mix of larger blocks + interleaved free (best-fit / freelist stress) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define N 400
void *ptrs[N];
int main()
{
    int i, fails = 0;
    for (i = 0; i < N; i++) {
        int sz = 64 + (i % 7) * 48;
        ptrs[i] = malloc(sz);
        if (!ptrs[i]) { fails++; break; }
        memset(ptrs[i], (char)(i & 0xff), 8);
    }
    for (i = 1; i < N; i += 3) {
        if (ptrs[i]) { free(ptrs[i]); ptrs[i] = NULL; }
    }
    for (i = 0; i < N; i += 5) {
        if (ptrs[i]) { free(ptrs[i]); ptrs[i] = NULL; }
    }
    for (i = 0; i < N; i++) {
        if (ptrs[i] == NULL) {
            int sz = 80 + (i % 5) * 32;
            ptrs[i] = malloc(sz);
            if (!ptrs[i]) fails++;
            else memset(ptrs[i], 0x11, 4);
        }
    }
    for (i = 0; i < N; i++)
        if (ptrs[i]) free(ptrs[i]);
    printf(fails ? "FAIL %d\n" : "ok\n", fails);
    return fails ? 1 : 0;
}
