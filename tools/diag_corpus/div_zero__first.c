#include <stdio.h>
int main()
{
    int i, d, x;
    d = 1;
    for (i = 0; i < 6; i++)
    {
        d = d - 1;
        x = 100 / d;
    }
    return 0;
}
