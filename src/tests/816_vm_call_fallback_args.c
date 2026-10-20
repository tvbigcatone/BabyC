/* A rejected compact-VM body must not replay a user call with arguments. */
#include <stdio.h>

static int total = 0;

static int add_to_total(int amount, int multiplier)
{
    total += amount * multiplier;
    return total;
}

int main(void)
{
    int result;
    result = add_to_total(3, 4);
    printf("%d %d\n", result, total);
    return 0;
}
