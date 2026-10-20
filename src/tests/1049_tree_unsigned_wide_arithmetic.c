#include <stdio.h>
static void test_ull(void) {
    unsigned long long s = 1, t = 6000000000ULL;
    s *= t; printf("%llu\n", s);
    s = 1; s = s * t; printf("%llu\n", s);
    s = 1; s += t; printf("%llu\n", s);
}
static void test_ul(void) {
    unsigned long s = 1, t = 6000000000UL;
    s *= t; printf("%lu\n", s);
    s = 1; s = s * t; printf("%lu\n", s);
    s = 1; s += t; printf("%lu\n", s);
}
int main(void) {
    unsigned long long s = 1, t = 6000000000ULL;
    s *= t; printf("%llu\n", s);
    s = 1; s = s * t; printf("%llu\n", s);
    s = 1; s += t; printf("%llu\n", s);
    test_ull();
    test_ul();
    return 0;
}
