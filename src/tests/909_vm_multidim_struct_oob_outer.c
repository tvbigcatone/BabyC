struct Cell { int value; int tag; };
int main(void) {
    struct Cell grid[2][3];
    int i = 2, j = 0;
    grid[i][j].value = 7;
    return 0;
}
