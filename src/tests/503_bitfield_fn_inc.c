#include <stdio.h>
struct S { int a:4; };
int inc_a(struct S *p) { return p->a++; }
int main() {
  struct S s;
  s.a = 2;
  printf("%d\n", inc_a(&s));
  printf("%d\n", s.a);
  printf("%d\n", inc_a(&s));
  printf("%d\n", s.a);
  return 0;
}
