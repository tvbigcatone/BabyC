#include <stdio.h>
struct Cell { int v; int (*op)(int, int); };
int add(int x, int y) { return x + y; }
int mul(int x, int y) { return x * y; }
struct Cell tab[2];
int main() {
    tab[0].v = 3; tab[0].op = add;
    tab[1].v = 4; tab[1].op = mul;
    printf("%d %d\n", tab[0].op(tab[0].v, 2), tab[1].op(tab[1].v, 2));
    return 0;
}
