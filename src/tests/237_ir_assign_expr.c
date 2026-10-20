#include <stdio.h>
int main()
{
    int a, b;
    b = (a = 3) + 4;
    printf("%d %d\n", a, b);
    return 0;
}
