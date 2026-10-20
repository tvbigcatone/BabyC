#include <stdio.h>
static long long f(int x, ...) { return 1099511627774LL + (long long)x - 1LL; }
typedef long long (*VarFn)(int, ...);
int main(void) {
    VarFn fp = f;
    printf("%lld\n", fp(1, 7, 9));
    return 0;
}
