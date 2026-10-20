#include <stdio.h>
struct S { int a; int b; };
int main(void) {
  int x = 10;
  int y = 20;
  struct S s = { x + 1, y - 5 };
  struct S t = { .b = x * 2, .a = y / 2 };
  printf("%d %d\n", s.a, s.b);
  printf("%d %d\n", t.a, t.b);
  return 0;
}
