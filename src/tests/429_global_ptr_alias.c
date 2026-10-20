#include <stdio.h>
int G[3];
int *gp;
int main() {
    int *p;
    G[0] = 1; G[1] = 2; G[2] = 3;
    gp = &G[0];
    p = gp + 1;
    *p = *p + 10;
    printf("%d %d %d\n", G[0], G[1], G[2]);
    return 0;
}
