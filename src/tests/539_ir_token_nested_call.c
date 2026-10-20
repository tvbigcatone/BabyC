/* Anti-regression: Body-IR main → token mid → Body-IR inner.
 * IrCallDepth=0 isolation must restore outer HasReturn. */
#include <stdio.h>
static int g = 10;
static int *inner(int *p) { return p; }
static int mid(void) {
  int x = 1;
  int *p = &g;
  int **pp = &p;
  /* switch forces token-path compile of mid */
  switch (0) { case 0: break; }
  --g;
  (*pp) = inner(&x);
  return g + x;
}
int main(void) {
  int r = mid();
  printf("%d\n", r);
  return 0;
}
