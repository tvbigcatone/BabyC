/* Direct regression: int / by zero must ProgramFail, not SIGFPE. */

#include <stdio.h>

int main()
{
    int a = 10;
    int b = 0;
    int c;
    c = a / b;
    printf("%d\n", c);
    return 0;
}
