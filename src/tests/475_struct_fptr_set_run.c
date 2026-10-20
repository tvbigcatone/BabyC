#include <stdio.h>
struct Obj { int (*op)(int, int); };
int add(int x, int y) { return x + y; }
int sub(int x, int y) { return x - y; }
struct Obj g;
void set_op(int (*f)(int, int)) { g.op = f; }
int run(int a, int b) { return g.op(a, b); }
int main() {
    set_op(add);
    printf("%d\n", run(10, 3));
    set_op(sub);
    printf("%d\n", run(10, 3));
    return 0;
}
