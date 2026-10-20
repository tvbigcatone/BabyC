#include <stdio.h>
void fill(int *r)
{
    r[0] = 1;
    r[1] = 2;
    r[2] = 3;
}
int main()
{
    int m[2][3] = {{0, 0, 0}, {0, 0, 0}};
    int u[2][3];
    int *q;
    fill(m[1]);
    printf("%d %d %d | %d %d %d\n", m[0][0], m[0][1], m[0][2], m[1][0], m[1][1], m[1][2]);
    fill(u[1]);
    q = u[0];
    q[0] = 4;
    q[1] = 5;
    q[2] = 6;
    printf("%d %d %d | %d %d %d\n", u[0][0], u[0][1], u[0][2], u[1][0], u[1][1], u[1][2]);
    return 0;
}
