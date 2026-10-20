/* Anti-regression: Body-IR large local arrays charge interpreter stack. */
#include <stdio.h>
void deep(int n)
{
    char pad[8192];
    pad[0] = (char)n;
    if (n > 0)
        deep(n - 1);
}
int main(void)
{
    deep(400);
    printf("should_not_reach\n");
    return 0;
}
