#include <strings.h>
#include <stdio.h>
int main(void)
{
    if (ffs(0) != 0) return 1;
    if (ffs(1) != 1) return 2;
    if (ffs(0x80000000U) != 32) { puts("ffs high bit"); return 3; }
    puts("BabyPOSIX ffs: ok");
    return 0;
}
