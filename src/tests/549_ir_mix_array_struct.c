#include <stdio.h>
struct P { int x; int y; };
int main(void) {
  int idx[3] = { 0, 1, 2 };
  struct P a[3];
  int i, t = 0;
  for (i = 0; i < 3; i++) {
    a[idx[i]].x = i + 1;
    a[idx[i]].y = (i + 1) * 10;
  }
  for (i = 0; i < 3; i++) t += a[i].x + a[i].y;
  printf("%d\n", t);
  return 0;
}
