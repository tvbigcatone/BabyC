#include <stdio.h>
/* Pointer +/- ++ and p+=n shadow parity. */
int main(void)
{
    int a[8];
    int *p;
    int i, s;
    for (i = 0; i < 8; i++) a[i] = i * 10;
    p = a;
    s = *(p + 3);
    p += 2;
    s += *p;
    p++;
    s += *p;
    p -= 1;
    printf("%d %d %d\n", s, *p, *(p + 1));
    return 0;
}
