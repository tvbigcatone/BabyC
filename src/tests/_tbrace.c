#include <stdio.h>
struct S { int a; int b; int c; };
int main(void) {
  struct S s = { 1, 2, 3 };
  printf("%d %d %d\n", s.a, s.b, s.c);
  return 0;
}
