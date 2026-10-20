#include <stdio.h>
struct S { int v; int w; };
int main(void) {
  struct S a[4] = { {1,10}, {2,20}, {3,30}, {4,40} };
  int i, t = 0;
  for (i = 0; i < 4; i++) {
    a[i].v += i;
    t += a[i].v + a[i].w;
  }
  printf("%d\n", t);
  return 0;
}
