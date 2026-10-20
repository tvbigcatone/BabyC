#include <stdio.h>
int more(int *p)
{
    *p = *p + 1;
    return *p < 4;
}
int main()
{
    int n;
    int c;
    n = 0;
    c = 0;
    do {
        c = c + 1;
    } while (more(&n));
    printf("%d %d\n", c, n);
    return 0;
}
