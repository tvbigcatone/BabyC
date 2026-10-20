#include <stdio.h>
#include <string.h>
/* ISOLATED: SEGV in opaque library must report line + function, not silent death */
int main()
{
    char *p;
    p = (char *)0x8;
    printf("%s\n", p);
    return 0;
}
