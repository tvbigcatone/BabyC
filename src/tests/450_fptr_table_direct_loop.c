/* tab[i](x) in for-loop body */
#include <stdio.h>
int f0(int x) { return x; }
int f1(int x) { return x + 1; }
int f2(int x) { return x + 2; }
int main() {
    int (*tab[3])(int);
    int i, s;
    tab[0] = f0; tab[1] = f1; tab[2] = f2;
    s = 0;
    for (i = 0; i < 3; i = i + 1)
        s = s + tab[i](10);
    printf("%d\n", s);
    return 0;
}
