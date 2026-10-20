#include <stdio.h>

int g0, g1;
int *get0(void) { return &g0; }
int *get1(void) { return &g1; }

int main() {
    int *(*tab[2])(void);
    int *p;
    tab[0] = get0;
    tab[1] = get1;
    p = tab[0]();
    *p = 7;
    p = tab[1]();
    *p = 8;
    printf("%d %d\n", g0, g1);
    *tab[0]() = 70;
    *tab[1]() = 80;
    printf("%d %d\n", g0, g1);
    return 0;
}
