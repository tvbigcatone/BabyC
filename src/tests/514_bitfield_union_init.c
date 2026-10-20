#include <stdio.h>
typedef int int32_t;
typedef signed char int8_t;

union U {
  int32_t f0;
  unsigned f1 : 21;
  int8_t *f2;
  signed f3 : 28;
};

/* first member is pointer — init must still target declaration-first */
union V {
  int8_t *p;
  unsigned b : 10;
  int n;
};

int main() {
  union U u = {6};
  printf("%d %u\n", u.f0, u.f1);
  u.f1 = 0x1fffff;
  printf("%u\n", u.f1);
  union V v = {0};
  printf("%d\n", v.p == 0);
  return 0;
}
