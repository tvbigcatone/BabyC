#include <stdio.h>
struct P { int x; int y; };
int main(void) {
  struct P a[3] = { {1,2}, {3,4}, {5,6} };
  struct P b[] = { 10, 20, 30, 40 };
  int i, t = 0;
  for (i = 0; i < 3; i++) t += a[i].x + a[i].y;
  printf("%d\n", t);
  printf("%d %d %d %d\n", b[0].x, b[0].y, b[1].x, b[1].y);
  return 0;
}
