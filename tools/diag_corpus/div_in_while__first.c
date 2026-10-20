#include <stdio.h>
int main()
{
    int i, d, x;
    d = 1;
    x = 0;
    i = 0;
    while (i < 6)
    {
        d = d - 1;
        x += 100 / d;
        i++;
    }
    return 0;
}
