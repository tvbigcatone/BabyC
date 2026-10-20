#include <stdio.h>
struct S { int (*f)(int); };
int id(int x) { return x; }
struct S s;
int main() {
    int a, b, c;
    a = 1; b = 2; c = 3;
    s.f = id;
    printf("%d %d\n", s.f(a + b + c), a + b + c);
    return 0;
}
