/* Regression: while(1) + break must resume after the loop (not re-enter body). */
#include <stdio.h>
int main()
{
    int i;
    i = 0;
    while (1)
    {
        if (i >= 3)
            break;
        printf("%d\n", i);
        i = i + 1;
    }
    printf("done %d\n", i);
    return 0;
}
