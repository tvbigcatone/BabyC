#include <stdio.h>
struct S { int a; int b; };
int main(void) {
  struct S v[2];
  int *q;
  v[0].a = 1; v[0].b = 2;
  v[1].a = 3; v[1].b = 4;
  q = &v[1].a;
  *q = 9;
  printf("%d %d %d %d\n", v[0].a, v[0].b, v[1].a, v[1].b);
  return 0;
}
