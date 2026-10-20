#include <stdio.h>

int main(void)
{
    char a[8] = "abc";
    char b[] = "xy";
    char c[3] = "xyz";
    printf("%s %d %d %d\n", a, (int)a[3], (int)a[4], (int)a[7]);
    printf("%s %c %c\n", b, c[0], c[2]);
    return 0;
}
