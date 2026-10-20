/* S2: signed bitfield arithmetic truncation */
#include <stdio.h>
struct S { int a:3; int b:4; };
int main() {
  struct S s;
  s.a = 3;   /* max positive for 3-bit signed */
  s.b = -8;  /* min for 4-bit signed */
  printf("%d %d\n", s.a, s.b);
  s.a += 1;  /* overflow → -4 */
  s.b -= 1;  /* overflow → 7 */
  printf("%d %d\n", s.a, s.b);
  s.a = -1;
  s.b = 7;
  printf("%d %d\n", s.a, s.b);
  return 0;
}
