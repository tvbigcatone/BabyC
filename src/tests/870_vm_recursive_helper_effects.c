#include <stdio.h>
int state = 1;
int helper(int x)
{
    state = state * 3 + x;
    return state;
}
int recur(int n)
{
    int value;
    if (n <= 0) return helper(2);
    value = recur(n - 1);
    return helper(value % 7 + n);
}
int main(void)
{
    int a = recur(3);
    int b = helper(4);
    printf("%d %d %d\n", a, b, state);
    return 0;
}
