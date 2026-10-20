#include <stdio.h>
struct S { int x; int y; };
union U { int i; char c[4]; };
int main() {
  static struct S l;
  union U u;
  printf("%d %d\n", l.x, l.y);
  u.i = 0x01020304;
  printf("%d\n", u.c[0] ? 1 : 0);
  l.x = 9;
  printf("%d\n", l.x);
  return 0;
}
