#include <stdio.h>
int main()
{
    int i, d, x;
    d = 3;
    x = 1000;
    for (i = 0; i < 6; i++)
    {
        d = d - 1;
        x /= d;
    }
    return 0;
}
