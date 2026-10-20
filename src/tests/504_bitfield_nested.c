#include <stdio.h>
struct Inner { int x:3; int y:5; };
struct Outer { struct Inner in; int z; };
int main() {
  struct Outer o;
  o.in.x = 3; o.in.y = 15; o.z = 99;
  printf("%d %d %d\n", o.in.x, o.in.y, o.z);
  o.in.x = -1;
  printf("%d\n", o.in.x);
  return 0;
}
