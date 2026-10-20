/* Cross: mutual recursion + global array + switch */
#include <stdio.h>

int buf[16];
int even(int n);
int odd(int n);

int even(int n)
{
    if (n <= 0)
        return 0;
    switch (n % 2) {
        case 0:
            buf[n] = n;
            return n + odd(n - 1);
        default:
            return odd(n);
    }
}

int odd(int n)
{
    if (n <= 0)
        return 0;
    if (n % 2 == 1) {
        buf[n] = -n;
        return n + even(n - 1);
    }
    return even(n);
}

int main()
{
    int s = even(5);
    printf("%d %d %d %d\n", s, buf[5], buf[4], buf[3]);
    return 0;
}
