#include <stdio.h>
/* Structural thin if+call for: for (i=0;i<N;i++) if (cond) f(i); */
int g;
void bump(int x) { g += x; }
int main() {
    int i;
    g = 0;
    for (i = 0; i < 20; i++)
        if (i % 2 == 0) bump(i);
    printf("%d\n", g);
    return 0;
}
