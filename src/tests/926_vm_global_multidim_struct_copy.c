#include <stdio.h>
struct Cell { int a; int b; };
struct Cell grid[2][3];
int main(void)
{
    int i = 0, j = 1, x = 1, y = 2;
    grid[i][j].a = 11; grid[i][j].b = 7;
    grid[x][y] = grid[i][j];
    printf("%d %d %d %d\n", grid[x][y].a, grid[x][y].b,
           grid[i][j].a, grid[i][j].b);
    return 0;
}
