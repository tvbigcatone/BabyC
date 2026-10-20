/* S2: by-value BF struct args and return of scalar from BF */
#include <stdio.h>
struct S { int a:5; int b:5; };
int add_ab(struct S s) { return s.a + s.b; }
struct S make(int x, int y) {
  struct S s;
  s.a = x; s.b = y;
  return s;
}
int main() {
  struct S s = make(7, 9);
  printf("%d\n", add_ab(s));
  printf("%d\n", add_ab(make(3, 4)));
  return 0;
}
