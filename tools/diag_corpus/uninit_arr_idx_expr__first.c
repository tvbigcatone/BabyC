#include <stdio.h>
int main()
{
    int a[6];
    int i, s;
    s = 0;
    for (i = 0; i < 0; i++)
        a[i] = i;
    for (i = 0; i < 6; i++)
        s = a[i + 0] * 2;
    return 0;
}
