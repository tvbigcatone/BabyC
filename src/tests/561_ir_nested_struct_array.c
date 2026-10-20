#include <stdio.h>
struct Inner { int a; int b; };
struct Outer { struct Inner in; int id; };
int main(void) {
  struct Outer v[2] = { {{1,2}, 10}, {{3,4}, 20} };
  int i, t = 0;
  for (i = 0; i < 2; i++) t += v[i].in.a + v[i].in.b + v[i].id;
  printf("%d\n", t);
  return 0;
}
