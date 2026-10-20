#include <stdio.h>
int main() {
    int x;
    int *p;
    int **pp;
    x = 7;
    p = &x;
    pp = &p;
    **pp = **pp + 1;
    printf("%d %d\n", x, *p);
    return 0;
}
