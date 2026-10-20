#include <stdio.h>
struct S { int a:2, b:3, c:3; };
int main() {
  struct S s;
  s.a = 1; s.b = 3; s.c = 2;
  printf("%d %d %d\n", s.a, s.b, s.c);
  printf("%d\n", (int)sizeof(struct S));
  return 0;
}
