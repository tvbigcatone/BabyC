#include <stdio.h>
struct Inner { int x; int y; };
struct Outer { struct Inner in; int z; };
int main(void) {
  struct Outer o = { {7, 8}, 9 };
  struct Outer *p;
  p = &o;
  p->in.x += 1;
  p->in.y += 2;
  p->z += 3;
  printf("%d %d %d\n", o.in.x, o.in.y, o.z);
  return 0;
}
