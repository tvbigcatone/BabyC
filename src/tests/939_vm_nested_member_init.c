#include <stdio.h>
struct Inner { int x; int y; };
struct Outer { struct Inner in; int z; };
int main(void) {
  struct Outer o;
  o.in.x = 10; o.in.y = 20; o.z = 30;
  o.in.x += 5;
  printf("%d %d %d\n", o.in.x, o.in.y, o.z);
  return 0;
}
