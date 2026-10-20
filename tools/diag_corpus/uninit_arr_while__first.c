#include <stdio.h>
int main()
{
    int a[6];
    int i;
    for (i = 0; i < 0; i++)
        a[i] = i;
    i = 0;
    while (a[i] < 100)
        i++;
    return 0;
}
