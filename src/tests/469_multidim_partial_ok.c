#include <stdio.h>
/* After writing m[1][0], that cell is init; other cells still uninit. */
int main()
{
    int m[2][3];
    m[1][0] = 4;
    printf("%d\n", m[1][0]);
    return 0;
}
