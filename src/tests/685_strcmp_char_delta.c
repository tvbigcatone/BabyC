/* Regression: strcmp must return unsigned-char delta, not just -1/0/1.
 * MSVC UCRT host strcmp often yields ±1; samples expect 'o'-'O' == 32. */
#include <stdio.h>
#include <string.h>

int main(void)
{
    char a[] = "Fondamenti";
    char b[] = "Fondamenti";
    char c[] = "FONDAMENTI";

    printf("strcmp(a,b)=%d\n", strcmp(a, b));
    printf("strcmp(a,c)=%d\n", strcmp(a, c));
    printf("strcmp(c,a)=%d\n", strcmp(c, a));
    printf("strncmp(a,c,1)=%d\n", strncmp(a, c, 1));
    printf("strncmp(a,c,2)=%d\n", strncmp(a, c, 2));
    return 0;
}
