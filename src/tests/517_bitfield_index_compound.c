/* p[bf] += n must not apply pointer scale to element value */
#include <stdio.h>
struct T { unsigned u:4; };
int main() {
  int p[16];
  struct T t;
  int i;
  for (i = 0; i < 16; i++) p[i] = i;
  t.u = 2;
  p[t.u] += 10;
  printf("%d\n", p[2]);
  t.u += 1;
  p[t.u] *= 2;
  printf("%d %d\n", t.u, p[3]);
  return 0;
}
