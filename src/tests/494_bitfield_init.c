#include <stdio.h>
struct S { int a:3; int b:5; unsigned c:4; };
int main() {
  struct S s = {1, 2, 3};
  printf("%d %d %u\n", s.a, s.b, s.c);
  return 0;
}
