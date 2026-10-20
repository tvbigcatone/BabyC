#include <stdio.h>
int even_visits = 0;
int odd_visits = 0;
int odd_step(int n);
int even_step(int n)
{
    even_visits = even_visits + 1;
    if (n <= 0) return 1;
    return n + odd_step(n - 1);
}
int odd_step(int n)
{
    odd_visits = odd_visits + 1;
    if (n <= 0) return 2;
    return n + even_step(n - 1);
}
int main(void)
{
    int a = even_step(4);
    int b = odd_step(3);
    printf("%d %d %d %d\n", a, b, even_visits, odd_visits);
    return 0;
}
