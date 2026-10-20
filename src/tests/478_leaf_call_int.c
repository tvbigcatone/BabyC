#include <stdio.h>
int add(int x, int y) { return x + y; }
int sub(int x, int y) { return x - y; }
int mul(int x, int y) { return x * y; }
int neg(int x) { return -x; }
int id(int x) { return x; }
int main() {
    int i, t;
    t = 0;
    for (i = 0; i < 100; i = i + 1)
        t = t + add(i, 1);
    printf("%d\n", t);
    printf("%d %d %d %d\n", sub(10, 3), mul(4, 5), neg(7), id(42));
    printf("%d\n", add(add(1, 2), add(3, 4)));
    return 0;
}
