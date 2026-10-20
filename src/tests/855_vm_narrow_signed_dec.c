#include <stdio.h>
int main(void)
{
    char c = 3; short s = 3; signed char d = 3; int i;
    for (i = 0; i < 10; i++) { c--; s--; d--; }
    printf("%d %d %d\n", c, s, d);
    return 0;
}
