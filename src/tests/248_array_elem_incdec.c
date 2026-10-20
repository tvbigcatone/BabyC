#include <stdio.h>
int main()
{
    int a[3];
    int m[2][3];
    a[1] = 1;
    a[1]++;
    a[1] += 2;
    a[2] = 9;
    a[2]--;
    m[1][0] = 4;
    m[0][2] = 5;
    m[1][0]++;
    printf("%d %d\n", a[1], a[2]);
    printf("%d %d\n", m[1][0], m[0][2]);
    return 0;
}
