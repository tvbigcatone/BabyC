#include <stdio.h>
static int values[3];
int main(void)
{
    int *p;
    values[0] = 10;
    values[1] = 20;
    values[2] = 30;
    p = values;
    if (p[0] != 10 || p[1] != 20 || p[2] != 30) return 1;
    p[1] = p[0] + p[2];
    if (values[1] != 40) return 2;
    printf("%d %d %d\n", p[0], p[1], p[2]);
    return 0;
}
