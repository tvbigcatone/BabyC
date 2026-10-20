/* Body-IR local struct with bitfields: read/write/assign-ops */
#include <stdio.h>
struct S { int a:5; int b:3; unsigned c:8; };
int main() {
  struct S s;
  s.a = 10; s.b = 3; s.c = 200;
  printf("%d %d %u\n", s.a, s.b, s.c);
  s.a += 1;
  s.b -= 1;
  s.c += 5;
  printf("%d %d %u\n", s.a, s.b, s.c);
  return 0;
}
