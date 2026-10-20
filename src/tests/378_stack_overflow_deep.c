#include <stdio.h>
/* Anti-regression: Body-IR local arrays must charge interpreter stack */
void deep(int n)
{
    // 4096 Only overflow 64 bit builds, 4352 32 bit too
    char pad[4352];
    pad[0] = (char)n;
    if (n > 0)
        deep(n - 1);
}
int main()
{
    deep(800);
    printf("should_not_reach\n");
    return 0;
}
