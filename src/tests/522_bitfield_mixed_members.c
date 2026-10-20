/* S2: mixed scalar + bitfield members in one struct local */
#include <stdio.h>
struct S { int x; int a:4; unsigned b:5; int y; };
int main() {
  struct S s;
  s.x = 100;
  s.a = 7;
  s.b = 17;
  s.y = 200;
  printf("%d %d %u %d\n", s.x, s.a, s.b, s.y);
  s.a += 1;
  s.b -= 1;
  s.x += s.a;
  printf("%d %d %u %d\n", s.x, s.a, s.b, s.y);
  return 0;
}
