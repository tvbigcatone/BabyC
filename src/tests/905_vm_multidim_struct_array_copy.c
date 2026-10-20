#include <stdio.h>
struct Cell { int value; int tag; };
int main(void) {
    struct Cell grid[2][3];
    int i = 1, j = 2, x = 0, y = 1;
    grid[i][j].value = 27;
    grid[i][j].tag = 9;
    grid[x][y] = grid[i][j];
    grid[i][j] = grid[x][y];
    printf("%d %d %d %d\n", grid[x][y].value, grid[x][y].tag,
           grid[i][j].value, grid[i][j].tag);
    return 0;
}
