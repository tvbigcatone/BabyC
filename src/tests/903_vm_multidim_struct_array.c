#include <stdio.h>
struct Cell { int value; int tag; };
int main(void) {
    struct Cell grid[2][3];
    int i, j;
    i = 1; j = 2;
    grid[0][0].value = 11;
    grid[i][j].value = 27;
    grid[i][j].tag = grid[0][0].value + 4;
    grid[0][1].value = grid[i][j].tag + grid[i][j].value;
    printf("%d %d %d %d\n", grid[0][0].value, grid[1][2].value,
           grid[1][2].tag, grid[0][1].value);
    return 0;
}
