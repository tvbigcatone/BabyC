#include <stdio.h>
int main()
{
    char c[6];
    int i, s;
    s = 0;
    for (i = 0; i < 4; i++)
        c[i] = 65 + i;
    for (i = 0; i < 6; i++)
        s += c[i];
    return 0;
}
