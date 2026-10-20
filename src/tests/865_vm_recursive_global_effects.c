#include <stdio.h>
int effects = 0;
int touch(int value)
{
    effects = effects + 1;
    return value + effects;
}
int descend(int n)
{
    if (n <= 0) return touch(10);
    return touch(descend(n - 1));
}
int main(void)
{
    int result = descend(3);
    printf("%d %d\n", result, effects);
    return 0;
}
