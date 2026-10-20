#include <stdio.h>
int first(char *s)
{
    return s[0];
}
void main()
{
    printf("%d\n", first("A"));
}
