#include <stdio.h>
int f(int n)
{
    int s;
    s = 0;
    do {
        s = s + n;
        n = n - 1;
    } while (n > 0);
    return s;
}
int main()
{
    printf("%d\n", f(4));
    return 0;
}
