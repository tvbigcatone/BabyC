#include <stdio.h>
static unsigned short grid[2][3];
static int signed_grid[2][2];
int main(void)
{
    grid[0][0] = 65535;
    grid[1][2] = 32768;
    signed_grid[0][1] = -17;
    signed_grid[1][0] = 5;
    signed_grid[1][0] += 9;
    if (grid[0][0] != 65535) return 1;
    if (grid[1][2] != 32768) return 2;
    if (signed_grid[0][1] != -17) return 3;
    if (signed_grid[1][0] != 14) return 5;
    grid[0][1] = grid[1][2];
    if (grid[0][1] != 32768) return 4;
    printf("%u %u %d %d\n", (unsigned int)grid[0][0], (unsigned int)grid[0][1], signed_grid[0][1], signed_grid[1][0]);
    return 0;
}
