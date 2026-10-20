#include <stdio.h>
int main()
{
    int *p;
    int s;
    p = 0;
    s = p[0];   /* token path: segmentation fault instead of a diagnostic */
    printf("%d\n", s);
    return 0;
}
