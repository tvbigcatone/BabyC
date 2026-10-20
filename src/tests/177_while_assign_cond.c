#include <stdio.h>
int main()
{
    int i, c;
    i = 0;
    while ((c = i) < 3)
    {
        printf("%d\n", c);
        i = i + 1;
    }
    printf("after %d\n", i);
    return 0;
}
