#include <stdio.h>
struct S { int a; int b; int c; };
int main(void) {
  struct S s = { .b = 2, .a = 1, .c = 3 };
  struct S t = { .c = 9 };
  printf("%d %d %d\n", s.a, s.b, s.c);
  printf("%d %d %d\n", t.a, t.b, t.c);
  return 0;
}
