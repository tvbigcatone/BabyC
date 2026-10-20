#include <stdio.h>
int main()
{
    int i, n, s;
    n = 50;
    i = 0;
    s = 0;
    while (i < n)
    {
        s = s + i;
        i = i + 1;
    }
    printf("%d\n", s);
    return 0;
}
