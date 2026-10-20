/* Anti-regression: nested brace struct init inside while/for.
 * Skip path must use member type, not parent (was: '}' expected). */
#include <stdio.h>
typedef unsigned char uint8_t;
typedef short int16_t;
struct S0 { uint8_t f0; uint8_t f1; int16_t f2; };
struct S1 { struct S0 f0; int y; };
int main(void) {
  int x = 0, t = 0;
  while (x < 2) {
    struct S1 s = {{1U, 2U, 3}, 4};
    t += (int)s.f0.f0 + (int)s.f0.f1 + (int)s.f0.f2 + s.y;
    x++;
  }
  printf("%d\n", t);
  return 0;
}
