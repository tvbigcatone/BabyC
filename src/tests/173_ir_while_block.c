#include <stdio.h>
int main()
{
    int i, a, b;
    i = 0; a = 0; b = 0;
    while (i < 5)
    {
        a = a + i;
        b = b + 1;
        i = i + 1;
    }
    printf("%d %d\n", a, b);
    return 0;
}
