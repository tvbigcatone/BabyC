#include <stdio.h>
struct Cell { int a; int b; };
struct Cell grid[2][3];
int main(void)
{
    int i = 1, j = 2, x = 0, y = 1;
    grid[i][j].a = 41;
    grid[i][j].b = 9;
    grid[x][y].a = grid[i][j].a + 1;
    grid[x][y].b = grid[i][j].b + 2;
    printf("%d %d %d %d\n", grid[i][j].a, grid[i][j].b,
           grid[x][y].a, grid[x][y].b);
    return 0;
}
