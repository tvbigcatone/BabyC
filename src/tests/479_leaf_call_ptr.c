#include <stdio.h>
int load(int *p) { return *p; }
void store(int *p, int v) { *p = v; }
int main() {
    int x;
    x = 0;
    store(&x, 99);
    printf("%d\n", load(&x));
    store(&x, load(&x) + 1);
    printf("%d\n", x);
    return 0;
}
