#include <stdio.h>
void main() {
  long long a = 1LL << 40;
  long long b = a + 1LL;
  unsigned long long u = 0xFFFFFFFFFFFFFFFFULL;
  printf("%lld\n", a);
  printf("%lld\n", b);
  printf("%llu\n", u);
}
