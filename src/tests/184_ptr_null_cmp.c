#include <stdio.h>
int main()
{
    int x;
    int *p;
    x = 7;
    p = 0;
    printf("%d\n", p == 0);
    p = &x;
    printf("%d\n", p != 0);
    printf("%d\n", *p);
    return 0;
}
