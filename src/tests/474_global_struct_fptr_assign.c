#include <stdio.h>
struct Obj { int a; int (*op)(int, int); };
int add(int x, int y) { return x + y; }
int mul(int x, int y) { return x * y; }
struct Obj o;
int main() {
    o.a = 5;
    o.op = add;
    printf("%d\n", o.op(o.a, 7));
    o.op = mul;
    printf("%d\n", o.op(o.a, 7));
    return 0;
}
