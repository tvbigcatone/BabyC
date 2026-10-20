#include <stdio.h>
int main()
{
    int a[6];
    int i, b;
    b = 3;
    for (i = 0; i < 4; i++)
        a[i] = i;
    for (i = 0; i < 6; i++)
        a[i] += b * 2;
    return 0;
}
