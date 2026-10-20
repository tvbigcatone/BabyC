/* table dispatch via load pattern */
#include <stdio.h>
int f0(int x) { return x + 0; }
int f1(int x) { return x + 1; }
int f2(int x) { return x + 2; }
int main() {
    int (*tab[3])(int);
    int (*fp)(int);
    int i, s;
    tab[0] = f0; tab[1] = f1; tab[2] = f2;
    s = 0;
    for (i = 0; i < 3; i = i + 1) {
        fp = tab[i];
        s = s + fp(10);
    }
    fp = tab[1];
    s = s + fp(1);
    printf("%d\n", s);
    return 0;
}
