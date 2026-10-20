#include <stdio.h>
struct S { unsigned a:4; unsigned b:4; };
int main() {
  struct S s;
  s.a = 1; s.b = 2;
  s.a += 2;
  s.b |= 1;
  printf("%u %u\n", s.a, s.b);
  s.a <<= 1;
  printf("%u\n", s.a);
  return 0;
}
