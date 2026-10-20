#include <stdio.h>
struct Inner { int x:3; int y:3; };
struct Outer { struct Inner in; int z:4; };
int main() {
  int p[8];
  struct Outer o;
  int i;
  for (i = 0; i < 8; i++) p[i] = i + 20;
  o.in.x = 1;
  o.in.y = 2;
  o.z = 3;
  printf("%d\n", p[o.in.x++]);
  printf("%d %d\n", o.in.x, o.in.y);
  printf("%d\n", p[o.z++]);
  printf("%d\n", o.z);
  return 0;
}
