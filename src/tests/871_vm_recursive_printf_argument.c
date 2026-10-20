#include <stdio.h>
int visits = 0;
int recursive_value(int n)
{
    visits = visits + 1;
    if (n <= 0) return visits * 2;
    return n + recursive_value(n - 1);
}
int main(void)
{
    printf("result=%d\n", recursive_value(6));
    printf("visits=%d\n", visits);
    return 0;
}
