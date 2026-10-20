#include <stdio.h>
typedef unsigned short (*F)(void);
unsigned short us_fn(void) { return 40000; }
int main(void)
{
    F fp = us_fn;
    printf("%d\n", (int)(fp() >> 1));
    return 0;
}
