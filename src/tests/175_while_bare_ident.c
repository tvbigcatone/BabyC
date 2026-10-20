#include <stdio.h>
int main()
{
    int x;
    x = 3;
    while (x)
    {
        printf("%d\n", x);
        x = x - 1;
    }
    printf("after\n");
    return 0;
}
