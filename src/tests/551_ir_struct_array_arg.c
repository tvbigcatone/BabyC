#include <stdio.h>
struct P { int x; int y; };
int sum(struct P *p, int n) {
  int i, t = 0;
  for (i = 0; i < n; i++) t += p[i].x + p[i].y;
  return t;
}
int main(void) {
  struct P a[3];
  int i;
  for (i = 0; i < 3; i++) { a[i].x = i; a[i].y = i * 2; }
  printf("%d\n", sum(a, 3));
  return 0;
}
