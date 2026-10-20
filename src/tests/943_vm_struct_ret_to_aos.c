#include <stdio.h>
struct S { int a; int b; };
struct S mk(int x, int y) { struct S r; r.a = x; r.b = y; return r; }
int main(void) {
  struct S v[2];
  v[0] = mk(1, 2);
  v[1] = mk(3, 4);
  printf("%d %d %d %d\n", v[0].a, v[0].b, v[1].a, v[1].b);
  return 0;
}
