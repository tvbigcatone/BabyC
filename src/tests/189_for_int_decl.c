#include <stdio.h>
int sum_n(int n)
{
    int s;
    s = 0;
    for (int i = 0; i < n; i = i + 1)
        s = s + i;
    return s;
}
int main()
{
    printf("%d\n", sum_n(6));
    return 0;
}
