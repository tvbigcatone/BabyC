#include <stdio.h>
struct S { unsigned a:4; unsigned b:4; unsigned c:4; unsigned d:4; };
int main(void) {
    struct S s;
    s.a = 1; s.b = 2; s.c = 3; s.d = 4;
    printf("%u %u %u %u\n", s.a, s.b, s.c, s.d);
    return 0;
}
