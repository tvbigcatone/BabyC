#include <stdio.h>
/* Exercise compact-VM indexed access for signed-char and unsigned-long-long arrays. */
static signed char sc[3];
static unsigned long long ull[3];

int main(void)
{
    int i;
    i = 0;
    while (i < 3)
    {
        sc[i] = i - 2;
        ull[i] = ~0ULL - (unsigned long long)i;
        i++;
    }
    if (sc[0] != -2 || sc[2] != 0) return 1;
    if (ull[0] != ~0ULL || ull[2] != (~0ULL - 2ULL)) return 2;
    printf("%d %d | %llu %llu\n", sc[0], sc[2], ull[0], ull[2]);
    return 0;
}
