#include <stdio.h>
static int cells[4];
static int total;
int main(void)
{
    int i = 0;
    while (i < 4) { cells[i] = i + 10; i++; }
    cells[1] = cells[0] + cells[3];
    total = cells[1];
    cells[3] = total - cells[0];
    if (cells[0] != 10 || cells[1] != 23 || cells[3] != 13 || total != 23) return 1;
    printf("%d %d %d %d\n", cells[0], cells[1], cells[3], total);
    return 0;
}
