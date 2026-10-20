/* Anti-regression: y = table[i] struct by-value from global aos under vm_strict. */
#include <stdio.h>
typedef struct { int a; int b; int c; } S;
S table[3];
int main(void) {
    S y;
    table[2].a = 10; table[2].b = 20; table[2].c = 30;
    y = table[2];
    printf("%d %d %d\n", y.a, y.b, y.c);
    return 0;
}
