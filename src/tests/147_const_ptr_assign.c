#include <stdio.h>
int main()
{
    const char *p;
    const char *q;
    p = "hello";
    q = p;
    p = "world";
    printf("%s %s\n", p, q);
    return 0;
}
