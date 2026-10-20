/* The compact VM may execute a fixed-arity scalar user call in a safe tail.
 * The callee then performs a second approved terminal call to printf. */
#include <stdio.h>

static int add(int a, int b)
{
    return a + b;
}

static int report_sum(int unused)
{
    printf("%d\n", add(5, 7));
    return unused;
}

int main(void)
{
    return report_sum(0);
}
