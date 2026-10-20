#include <stdio.h>
int mix(int a, char *p, int b)
{
    return a + b;
}
void main()
{
    printf("%d\n", mix(10, "hi", 7));
}
