#include <stdio.h>
#include <stdarg.h>
long long a1(int n, ...) { va_list ap; long long s = 0; va_start(ap, n); s = s + va_arg(ap, long long); va_end(ap); return s; }
long long a2(int n, ...) { va_list ap; long long s = 0; va_start(ap, n); s += (long long)va_arg(ap, long long); va_end(ap); return s; }
long long a3(int n, ...) { va_list ap; long long s; va_start(ap, n); s = va_arg(ap, long long) * 1; va_end(ap); return s; }
double a4(int n, ...) { va_list ap; double s = 0; va_start(ap, n); s += va_arg(ap, double); va_end(ap); return s; }
unsigned long long a5(int n, ...) { va_list ap; unsigned long long s = 1; va_start(ap, n); s *= va_arg(ap, unsigned long long); va_end(ap); return s; }
int main(void) {
  printf("%lld\n", a1(1, 5000000000LL)); printf("%lld\n", a2(1, 5000000000LL));
  printf("%lld\n", a3(1, 5000000000LL)); printf("%.2f\n", a4(1, 2.75));
  printf("%llu\n", a5(1, 6000000000ULL)); return 0; }
