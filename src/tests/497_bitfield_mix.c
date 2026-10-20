#include <stdio.h>
struct S { int a:3; int n; int b:5; };
int main() {
  struct S s;
  s.a = 1; s.n = 99; s.b = 7;
  printf("%d %d %d\n", s.a, s.n, s.b);
  printf("%d\n", (int)sizeof(struct S));
  return 0;
}
