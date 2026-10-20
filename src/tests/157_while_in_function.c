#include <stdio.h>
int sum_while(int n)
{
    int i, s;
    i = 0;
    s = 0;
    while (i < n)
    {
        s = s + i;
        i = i + 1;
    }
    return s;
}
int main()
{
    printf("%d\n", sum_while(10));
    return 0;
}
