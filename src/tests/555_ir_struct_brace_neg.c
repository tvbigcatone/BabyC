#include <stdio.h>
struct S { int a; int b; };
int main(void) {
  struct S s = { -3, -7 };
  struct S a[2] = { {-1, 2}, {3, -4} };
  printf("%d %d\n", s.a, s.b);
  printf("%d %d %d %d\n", a[0].a, a[0].b, a[1].a, a[1].b);
  return 0;
}
