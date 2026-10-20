#include <stdio.h>
struct S { int a:3; int b:5; unsigned c:4; };
int main() {
  struct S s;
  s.a = 3; s.b = 15; s.c = 7;
  printf("%d %d %u\n", s.a, s.b, s.c);
  s.a = -1;
  printf("%d\n", s.a);
  s.b = s.b + 1;
  printf("%d\n", s.b);
  return 0;
}
