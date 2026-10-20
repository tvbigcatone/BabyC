#include <stdio.h>
struct S { int a; int b; int c; };
int main(void) {
  struct S x = { 1 };
  struct S y = { 2, 3 };
  struct S z = { 7, 8, 9 };
  printf("%d %d %d\n", x.a, x.b, x.c);
  printf("%d %d %d\n", y.a, y.b, y.c);
  printf("%d %d %d\n", z.a, z.b, z.c);
  return 0;
}
