#include <stdio.h>
int main()
{
    int i = 5;
    int a, b, c;
    a = i++;
    b = ++i;
    c = i--;
    printf("%d %d %d %d\n", a, b, c, i);
    return 0;
}
