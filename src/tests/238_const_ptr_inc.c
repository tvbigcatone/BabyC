#include <stdio.h>
int sumc(const char *s)
{
    int c;
    int n = 0;
    while ((c = *s++))
        n = n + c;
    return n;
}
int main()
{
    printf("%d\n", sumc("ab"));
    printf("%d\n", sumc("hello"));
    return 0;
}
