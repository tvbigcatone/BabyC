#include <stdio.h>
struct Cell { int value; int tag; };
struct Cell grid[3];
int main(void) {
    int i = 1, j = 2;
    grid[i].value = 27;
    grid[i].tag = 9;
    grid[j].value = grid[i].value + 3;
    grid[j].tag = grid[i].tag + 1;
    printf("%d %d %d %d\n", grid[i].value, grid[i].tag,
           grid[j].value, grid[j].tag);
    return 0;
}
