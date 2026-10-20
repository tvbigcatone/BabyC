#include <stdio.h>
struct S { int v[4]; int n; };
int main(void) {
  struct S s = {{1,2,3,4}, 4};
  int i, t = 0;
  for (i = 0; i < s.n; i++) {
    s.v[i] *= 2;
    t += s.v[i];
  }
  printf("%d\n", t);
  return 0;
}
