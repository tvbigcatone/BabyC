/* Bug #1: HeapAllocStack azzera solo Size byte, non MEM_ALIGN(Size). */

#include <stdio.h>
#include <stdlib.h>

void main()
{
    long long a;
    long long b;
    long long c;
    int i;

    a = 0x1122334455667788LL;
    (void)a;

    for (i = 0; i < 100; i++)
    {
        long long x = (long long)i * 0x100000000LL + i;
        long long y = x + 1;
        if (y - x != 1)
        {
            printf("PADDING LEAK at %d: %lld %lld\n", i, x, y);
            return;
        }
    }

    b = 0;
    c = 0;
    printf("%lld %lld\n", b, c);
    printf("done\n");
}