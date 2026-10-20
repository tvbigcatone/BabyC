#include <stdio.h>
int next(void)
{
    static int k;
    k = k + 1;
    return k;
}
int main()
{
    int s;
    s = 0;
    while (next() < 4)
        s = s + 1;
    printf("%d\n", s);
    return 0;
}
