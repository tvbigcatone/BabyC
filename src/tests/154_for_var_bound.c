#include <stdio.h>
int main()
{
    int i, n, s;
    n = 100;
    s = 0;
    for (i = 0; i < n; i = i + 1)
        s = s + i;
    printf("%d\n", s);
    n = 5;
    s = 0;
    for (i = 0; i < n; i++)
        s = s + 1;
    printf("%d\n", s);
    return 0;
}
