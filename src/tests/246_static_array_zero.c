#include <stdio.h>
static int a[4];
int main()
{
    static char buf[3];
    printf("%d %d %d %d\n", a[0], a[1], a[2], a[3]);
    printf("%d %d %d\n", buf[0], buf[1], buf[2]);
    a[1] = 9;
    printf("%d\n", a[1]);
    return 0;
}
