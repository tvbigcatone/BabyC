#include <stdio.h>
struct Cell { int v; int w; };
int main(void) {
  struct Cell m[3];
  int i, j, t = 0;
  for (i = 0; i < 3; i++) { m[i].v = 0; m[i].w = 0; }
  for (i = 0; i < 3; i++)
    for (j = 0; j < 5; j++) {
      m[i].v += j;
      m[i].w += 1;
    }
  for (i = 0; i < 3; i++) t += m[i].v + m[i].w;
  printf("%d\n", t);
  return 0;
}
