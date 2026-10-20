#include <stdio.h>
int main()
{
    int a[6];
    int i;
    for (i = 0; i < 0; i++)
        a[i] = i;
    for (i = 0; i < 6; i++)
        a[i] += 2;
    return 0;
}
