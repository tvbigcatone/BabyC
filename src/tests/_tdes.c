#include <stdio.h>
struct S { int a; int b; int c; };
int main(void) {
  struct S s = { .b = 2, .a = 1, .c = 3 };
  printf("%d %d %d\n", s.a, s.b, s.c);
  return 0;
}
