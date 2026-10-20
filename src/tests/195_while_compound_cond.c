#include <stdio.h>
int main()
{
    int i, j, s;
    i = 0;
    j = 10;
    s = 0;
    while (i < 5 && j > 5)
    {
        s = s + 1;
        i = i + 1;
        j = j - 1;
    }
    printf("%d %d %d\n", s, i, j);
    return 0;
}
