#include <stdio.h>
struct S { int x:3; };
int main() {
  struct S s;
  s.x = 7;  /* 111 -> -1 in 3-bit signed */
  printf("%d\n", s.x);
  s.x = 4;  /* 100 -> -4 */
  printf("%d\n", s.x);
  s.x = 3;
  printf("%d\n", s.x);
  return 0;
}
