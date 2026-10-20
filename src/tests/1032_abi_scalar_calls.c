#include <stdio.h>

static char rc(char x) { return x; }
static unsigned char ruc(unsigned char x) { return x; }
static short rs(short x) { return x; }
static unsigned short rus(unsigned short x) { return x; }
static int ri(int x) { return x; }
static unsigned int ru(unsigned int x) { return x; }
static long rl(long x) { return x; }
static unsigned long rul(unsigned long x) { return x; }
static long long rll(long long x) { return x; }
static unsigned long long rull(unsigned long long x) { return x; }
static float rf(float x) { return x; }
static double rd(double x) { return x; }

static int plus(int x) { return x + 7; }
static int apply(int (*fn)(int), int x) { return fn(x); }
static int rec(int n, int (*fn)(int)) {
    if (n <= 0) return 0;
    return fn(n) + rec(n - 1, fn);
}
static int bump(int x) { return x + 1; }
static int side;
static int mark(int x) { side += x; return x; }
static int pair(int a, int b) { return a * 10 + b; }
static int callside(void) { side = 0; return pair(mark(1), pair(mark(2), mark(3))); }
static int *idp(int *p) { return p; }

int main(void) {
    int x = 19;
    int (*fp)(int) = plus;
    printf("%d %u %d %u %d %u\n", rc(-7), ruc(250), rs(-300), rus(60000), ri(-123456), ru(4000000000U));
    printf("%ld %lu %lld %llu\n", rl(-1234567L), rul(4000000000UL), rll(-1234567890123LL), rull(12345678901234ULL));
    printf("%.1f %.2f\n", rf(1.25f), rd(2.125));
    printf("%d %d %d\n", apply(fp, 5), apply(bump, 8), rec(4, bump));
    printf("%d %d %d\n", idp(&x) == &x, idp(NULL) == NULL, x);
    { int result = callside(); printf("%d %d\n", result, side); }
    return 0;
}
