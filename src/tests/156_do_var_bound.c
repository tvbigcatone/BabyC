#include <stdio.h>
int main()
{
    int i, n, s;
    n = 20;
    i = 0;
    s = 0;
    do {
        s = s + i;
        i = i + 1;
    } while (i < n);
    printf("%d\n", s);
    return 0;
}
