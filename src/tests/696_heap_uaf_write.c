#include <stdio.h>
#include <stdlib.h>
int main(void)
{
    int *p = (int *)malloc(sizeof(int) * 2);
    p[0] = 1;
    free(p);
    p[0] = 9;
    return 0;
}
