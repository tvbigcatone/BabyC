#include <stdio.h>
int inc(int x) { return x + 1; }
struct H { int (*op)(int); int acc; };
int main() {
  struct H h;
  int i;
  h.op = &inc;
  h.acc = 0;
  for (i = 0; i < 5; i++)
    h.acc = (*h.op)(h.acc);
  printf("acc %d\n", h.acc);
  return 0;
}
