/* S2: bitfield used in for condition and index */
#include <stdio.h>
struct S { int n:8; int cur:8; };
int main() {
  struct S s;
  int a[10], i, t;
  s.n = 5;
  s.cur = 0;
  for (i = 0; i < 10; i++) a[i] = i * 10;
  t = 0;
  for (s.cur = 0; s.cur < s.n; s.cur++)
    t += a[s.cur];
  printf("%d %d\n", t, s.cur);
  return 0;
}
