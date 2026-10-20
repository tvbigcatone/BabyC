#include <stdio.h>
int a0(int x) { return x; }
int a1(int x) { return x + 1; }
int main() {
    int (*f)(int);
    int (*g)(int);
    int (*t)(int);
    f = a0;
    g = a1;
    t = f; f = g; g = t;
    printf("%d %d\n", f(10), g(10));
    return 0;
}
