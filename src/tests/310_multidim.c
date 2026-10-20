#include <stdio.h>
void main()
{
    int a[5][4];
    int i, j, sum;
    for (i = 0; i < 5; i++)
        for (j = 0; j < 4; j++)
            a[i][j] = i * 10 + j;
    sum = 0;
    for (i = 0; i < 5; i++)
        for (j = 0; j < 4; j++)
            sum = sum + a[i][j];
    printf("sum %d\n", sum);
}
