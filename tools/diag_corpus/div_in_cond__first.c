#include <stdio.h>
int main()
{
    int i, d, x;
    d = 1;
    x = 0;
    for (i = 0; i < 6; i++)
    {
        d = d - 1;
        if (100 / d > 3) x = 1;
    }
    return 0;
}
