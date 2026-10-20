/* Anti-regression: Body-IR recursion must fail with message before host stack dies */
#include <stdio.h>
void deep(int n)
{
    // 4096 Only overflow 64 bit builds, 4352 32 bit too
    char pad[4352];
    pad[0] = (char)n;
    if (n > 0)
        deep(n - 1);
}
int main(void)
{
    deep(800);
    printf("should_not_reach\n");
    return 0;
}
