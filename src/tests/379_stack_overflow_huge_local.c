#include <stdio.h>
/* Anti-regression: automatic array larger than stack must fail cleanly */
int main()
{
    int big[600000];
    big[0] = 1;
    printf("should_not_reach %d\n", big[0]);
    return 0;
}
