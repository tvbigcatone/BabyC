#include <stdio.h>
struct S { int a:5; int b:5; int c:6; };
int main(void) {
  struct S a[2] = { {1,2,3}, {4,5,6} };
  printf("%d %d %d %d %d %d\n", a[0].a, a[0].b, a[0].c, a[1].a, a[1].b, a[1].c);
  return 0;
}
