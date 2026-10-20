#include <stdio.h>
struct S { int a; int b; };
int main(void) {
  struct S v[3];
  v[0].a = 1;
  v[0].b = 2;
  printf("%d %d\n", v[0].a, v[0].b);
  printf("%d\n", v[2].a);
  return 0;
}
