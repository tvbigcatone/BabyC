/* direct tab[i](x) on token path — no intermediate load */
#include <stdio.h>
int add1(int x) { return x + 1; }
int mul2(int x) { return x * 2; }
int neg(int x) { return -x; }
int main() {
    int (*tab[3])(int);
    int i, s;
    tab[0] = add1;
    tab[1] = mul2;
    tab[2] = neg;
    printf("%d\n", tab[0](10));
    printf("%d\n", tab[1](7));
    s = 0;
    for (i = 0; i < 3; i = i + 1)
        s = s + tab[i](10);
    printf("%d\n", s);
    return 0;
}
