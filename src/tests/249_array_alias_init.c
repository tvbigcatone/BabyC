#include <stdio.h>
#include <string.h>
void fill(int *q, int n)
{
    int i;
    for (i = 0; i < n; i++)
        q[i] = i * 2;
}
int main()
{
    char s[8];
    int z[4];
    int f[4];
    int p[3];
    int t[3];
    int *q;
    strcpy(s, "hi");
    memset(z, 0, sizeof(z));
    fill(f, 4);
    q = &p[1];
    q[0] = 7;
    q[-1] = 6;
    q[1] = 8;
    q = t;
    q[2] = 5;
    printf("%c %d %d %d %d %d %d\n", s[0], z[2], f[3], p[0], p[1], p[2], t[2]);
    printf("%d\n", (int)sizeof(f[1]));
    return 0;
}
