/* atoi available without #include <stdlib.h> */
#include <stdio.h>
int main()
{
    printf("%d\n", atoi("123"));
    printf("%d\n", atoi("-99"));
    printf("%d\n", atoi("  7"));
    return 0;
}
