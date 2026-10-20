/* bitfield struct return-by-value + assign (100% Body-IR form) */
#include <stdio.h>
struct S { int a:5; int b:3; };
struct S make(int x, int y) {
  struct S s;
  s.a = x; s.b = y;
  return s;
}
int main() {
  struct S t;
  t = make(10, 3);
  printf("%d %d\n", t.a, t.b);
  t = make(7, 1);
  printf("%d %d\n", t.a, t.b);
  return 0;
}
