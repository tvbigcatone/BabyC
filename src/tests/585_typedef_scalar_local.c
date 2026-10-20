/* Anti-regression: Body-IR scalar typedef locals */
#include <stdio.h>
typedef int i32;
typedef unsigned int u32;
typedef long long i64;
int main() {
    i32 a = 10;
    u32 b = 20;
    i64 c = 30;
    a = a + 1;
    b = b + 2;
    c = c + 3;
    printf("%d %u %lld\n", a, b, c);
    return 0;
}
