#include <stdio.h>
struct S { int a; int b; };
int sum(struct S *p, int n) {
  int i, t = 0;
  for (i = 0; i < n; i++) t += p[i].a + p[i].b;
  return t;
}
int main(void) {
  struct S a[3] = { {1,2}, {3,4}, {5,6} };
  printf("%d\n", sum(a, 3));
  return 0;
}
