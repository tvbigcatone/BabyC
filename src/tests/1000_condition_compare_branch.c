#include <stdio.h>
int main(void) {
    int x = 4, y = 9, n = 0;
    if (x < y) n = n + 1;
    if (x == 4 && y >= 9) n = n + 2;
    if (x != y || y < x) n = n + 4;
    for (x = 0; x < 3 && y > 7; x = x + 1) n = n + 1;
    while (x < 5 && (y == 9 || y == 10)) x = x + 1;
    printf("x=%d n=%d\n", x, n);
    return 0;
}
