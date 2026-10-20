#include <stdio.h>
struct S { int a; int b; };
int main(void) {
  struct S s;
  s.a = 1; s.b = 2;
  printf("%d %d\n", s.a, s.b);
  return 0;
}
