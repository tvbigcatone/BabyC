/* Anti-regression: global + local function pointer */
#include <stdio.h>
int add1(int x) { return x + 1; }
int mul2(int x) { return x * 2; }
int (*gfp)(int);
int main() {
    int (*fp)(int);
    gfp = add1;
    printf("%d\n", gfp(10));
    fp = mul2;
    printf("%d\n", fp(10));
    return 0;
}
