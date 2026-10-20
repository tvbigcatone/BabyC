/* for/while inside function body must run via IR when compilable */
#include <stdio.h>
int accum(int n)
{
    int i, s;
    s = 0;
    i = 0;
    while (i < n)
    {
        s = s + i;
        i = i + 1;
    }
    return s;
}
int main()
{
    printf("%d %d\n", accum(5), accum(1));
    return 0;
}
