#include <stdio.h>
int main()
{
    int a[6];
    int b[6];
    int i;
    for (i = 0; i < 4; i++)
        a[i] = i;
    for (i = 0; i < 6; i++)
        b[i] = a[i];
    return 0;
}
