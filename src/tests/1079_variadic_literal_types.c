#include <stdio.h>
#include <stdarg.h>
static unsigned long gu(int n, ...) { va_list ap; unsigned long v; va_start(ap, n); v = va_arg(ap, unsigned long); va_end(ap); return v; }
static long gl(int n, ...) { va_list ap; long v; va_start(ap, n); v = va_arg(ap, long); va_end(ap); return v; }
static long long gll(int n, ...) { va_list ap; long long v; va_start(ap, n); v = va_arg(ap, long long); va_end(ap); return v; }
static unsigned gui(int n, ...) { va_list ap; unsigned v; va_start(ap, n); v = va_arg(ap, unsigned); va_end(ap); return v; }
static int gi(int n, ...) { va_list ap; int v; va_start(ap, n); v = va_arg(ap, int); va_end(ap); return v; }
int main(void) {
    long x = -5L; int k = 3;
    printf("%lu %lu %lu\n", gu(1, 5UL), gu(1, 5ul), gu(1, 0xFFFFFFFFUL));
    printf("%ld %ld %ld %ld\n", gl(1, -1L), gl(1, 2000000000L), gl(1, x * k), gl(1, 0x7FFFFFFFL));
    printf("%lld %lld %lld\n", gll(1, 1LL), gll(1, -9LL), gll(1, 3000000000LL << 4));
    printf("%u %u %u\n", gui(1, 0xFFFFFFFF), gui(1, 7u), gui(1, 7));
    printf("%d %d %d %d\n", gi(1, 'A'), gi(1, (short)-2), gi(1, 1 + 2), gi(1, 4000000000u > 1));
    printf("%ld %lu %lld\n", 7L + 1, sizeof(int) + 0UL, 5LL * 2);
    return 0;
}
