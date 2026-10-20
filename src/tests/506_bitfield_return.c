#include <stdio.h>
struct S { int a:5; int b:3; };
struct S make(int x, int y) {
  struct S s;
  s.a = x; s.b = y;
  return s;
}
int main() {
  struct S t = make(10, 3);
  printf("%d %d\n", t.a, t.b);
  return 0;
}
