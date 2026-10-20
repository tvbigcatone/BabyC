#include <stdio.h>
typedef unsigned short (*convert_fn)(unsigned short);
static unsigned short twice(unsigned short x) { return (unsigned short)(x * 2U); }
static unsigned short invoke(convert_fn fn, unsigned short x) { return fn(x); }
static int callback(int x) { return x * 3; }
static int with_callback(int (*fn)(int), int x) { return fn(x) - 2; }
static int depth;
static int recursive(int n, int x) {
    if (n == 0) return x;
    return recursive(n - 1, x + (depth++));
}
int main(void) {
    convert_fn f = twice;
    int (*cb)(int) = callback;
    depth = 0;
    printf("%u %d %d\n", invoke(f, 32000U), with_callback(cb, 14), recursive(4, 10));
    printf("%d\n", depth);
    return 0;
}
