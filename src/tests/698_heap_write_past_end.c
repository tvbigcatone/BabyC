#include <stdio.h>
#include <stdlib.h>
int main(void)
{
    int *p = (int *)malloc(sizeof(int) * 2);
    p[2] = 3;
    return 0;
}
