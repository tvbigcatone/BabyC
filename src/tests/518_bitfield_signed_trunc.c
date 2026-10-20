#include <stdio.h>
struct S { int a:3; unsigned b:3; };
int main() {
  struct S s;
  s.a = 3;   /* fits */
  s.b = 7;
  printf("%d %u\n", s.a, s.b);
  s.a = 4;   /* 100 in 3 bits signed → -4 */
  printf("%d\n", s.a);
  return 0;
}
