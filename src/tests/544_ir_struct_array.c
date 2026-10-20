#include <stdio.h>
struct P { int x; int y; };
int main(void) {
  struct P a[4];
  int i, t = 0;
  for (i = 0; i < 4; i++) { a[i].x = i * 10; a[i].y = i; }
  for (i = 0; i < 4; i++) t += a[i].x + a[i].y;
  printf("%d %d %d\n", a[0].x, a[3].y, t);
  return 0;
}
