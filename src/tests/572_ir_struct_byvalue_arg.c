/* Anti-regression: struct by-value arg (plain + bitfield) Body-IR */
#include <stdio.h>
struct P { int x; int y; };
struct B { int x:5; unsigned y:7; };
int sumP(struct P s) { return s.x + s.y; }
int sumB(struct B s) { return (int)s.x + (int)s.y; }
int main(void) {
  struct P p; struct B b;
  p.x = 5; p.y = 20;
  b.x = 5; b.y = 20;
  printf("%d %d\n", sumP(p), sumB(b));
  return 0;
}
