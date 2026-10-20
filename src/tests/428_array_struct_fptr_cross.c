#include <stdio.h>
int f0(int x) { return x + 0; }
int f1(int x) { return x + 1; }
struct Cell { int (*fp)(int); int v; };
int main() {
    struct Cell t[2];
    int (*fp)(int);
    t[0].fp = f0; t[0].v = 10;
    t[1].fp = f1; t[1].v = 20;
    fp = t[0].fp;
    printf("%d\n", fp(t[0].v));
    fp = t[1].fp;
    printf("%d\n", fp(t[1].v));
    return 0;
}
