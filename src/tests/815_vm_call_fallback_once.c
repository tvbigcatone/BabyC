/* A compact-VM preflight rejection must not replay a side-effecting call. */
#include <stdio.h>

static int hits = 0;

static int bump(void)
{
    hits++;
    return hits;
}

int main(void)
{
    int result;
    result = bump();
    printf("%d %d\n", result, hits);
    return 0;
}
