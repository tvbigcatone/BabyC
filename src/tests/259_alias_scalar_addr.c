#include <stdio.h>
void set(int *p)
{
    *p = 3;
}
int main()
{
    int x;
    int y;
    int *q;
    set(&x);
    q = &y;
    *q = 4;
    printf("%d %d\n", x, y);
    return 0;
}
