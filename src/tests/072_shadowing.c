#include <stdio.h>

void main()
{
    int n = 1;
    printf("%d\n", n);
    {
        int n = 2;
        printf("%d\n", n);
        {
            int n = 3;
            printf("%d\n", n);
        }
        printf("%d\n", n);
    }
    printf("%d\n", n);
}
