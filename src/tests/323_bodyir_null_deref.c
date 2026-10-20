#include <stdio.h>
/* Body IR: NULL *p must fail cleanly */
int boom(char *p)
{
    return *p;
}
void main()
{
    boom(0);
}
