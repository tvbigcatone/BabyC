/* Isolated: *(p+(i&mask)), p[i], pointer subtract */
#include <stdio.h>
int main() {
    int a[8];
    int *p;
    int i, s = 0, d;
    for (i = 0; i < 8; i = i + 1) a[i] = (i + 1) * 3;
    p = a;
    for (i = 0; i < 8; i = i + 1)
        s = s + *(p + (i & 3));
    for (i = 0; i < 8; i = i + 1)
        s = s + p[i];
    d = (p + 3) - a;
    printf("%d %d\n", s, d);
    return 0;
}
