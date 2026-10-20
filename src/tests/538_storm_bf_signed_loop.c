/* storm distill: signed BF overflow/trunc under compound in for */
#include <stdio.h>
struct S { int n:4; unsigned m:4; };
int main(void) {
  struct S s;
  int i, t = 0;
  s.n = 0; s.m = 0;
  for (i = 0; i < 10; i++) {
    s.n += 1;
    s.m += 1;
    t += (int)s.n + (int)s.m;
  }
  printf("%d %d %d\n", (int)s.n, (int)s.m, t);
  return 0;
}
