/* while(1) uses FastCond Active==2 constant path */
#include <stdio.h>
int main()
{
    int n;
    n = 0;
    while (1)
    {
        n = n + 1;
        if (n == 4)
            break;
    }
    printf("%d\n", n);
    return 0;
}
