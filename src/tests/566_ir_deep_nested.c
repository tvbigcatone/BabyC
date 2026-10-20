#include <stdio.h>
struct A { int x; };
struct B { struct A a; int y; };
struct C { struct B b; int z; };
int main(void) {
  struct C c = { {{1}, 2}, 3 };
  c.b.a.x += 10;
  printf("%d %d %d\n", c.b.a.x, c.b.y, c.z);
  return 0;
}
