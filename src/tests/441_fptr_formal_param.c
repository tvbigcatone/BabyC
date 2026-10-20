/* function pointer as formal parameter */
#include <stdio.h>
int add1(int x) { return x + 1; }
int mul2(int x) { return x * 2; }
int apply(int (*fp)(int), int x) { return fp(x); }
int apply2(int (*fp)(int), int x, int y) { return fp(x) + fp(y); }
int main() {
    printf("%d\n", apply(add1, 10));
    printf("%d\n", apply(mul2, 7));
    printf("%d\n", apply2(add1, 3, 4));
    printf("%d\n", apply2(mul2, 3, 4));
    return 0;
}
