#include <stdio.h>
/* Soft parity: *p / s += *p after p+= in separate stmts (F5 residual). */
int main(void)
{
    int a[8];
    int *p;
    int i, s = 0;
    for (i = 0; i < 8; i++) a[i] = i * 10;
    p = a;
    p += 2;
    s += *p;
    p++;
    s += *p;
    *p = 99;
    s += *p;
    printf("%d %d\n", s, a[3]);
    return 0;
}
