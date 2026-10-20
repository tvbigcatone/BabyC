#include <stdio.h>
int acc(int n)
{
    int i;
    int s;
    s = 0;
    i = 0;
    while (i < n) {
        s = s + i;
        i = i + 1;
    }
    return s + n;
}
int main()
{
    printf("%d\n", acc(5));
    return 0;
}
