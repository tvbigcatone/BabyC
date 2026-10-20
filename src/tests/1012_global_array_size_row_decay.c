#include <stdio.h>

static char grid[2][4] = { "cat", "dog" };

int main(int argc, char **argv)
{
    printf("%ld %s %c %s %c\n", (long)sizeof(grid), grid[0], grid[0][1],
           grid[1], grid[1][1]);
    return 0;
}
