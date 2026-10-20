/* for-loop body token-cache inside a function (hanoi-class case) */
#include <stdio.h>
int sum_to(int n)
{
    int i, s;
    s = 0;
    for (i = 0; i < n; i = i + 1)
        s = s + i;
    return s;
}
int main()
{
    printf("%d %d\n", sum_to(5), sum_to(10));
    return 0;
}
