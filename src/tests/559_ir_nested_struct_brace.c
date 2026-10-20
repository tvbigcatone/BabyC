#include <stdio.h>
struct Inner { int x; int y; };
struct Outer { struct Inner in; int z; };
int main(void) {
  struct Outer o = { {1, 2}, 3 };
  struct Outer p = { {4, 5} }; /* partial: z=0 */
  printf("%d %d %d\n", o.in.x, o.in.y, o.z);
  printf("%d %d %d\n", p.in.x, p.in.y, p.z);
  return 0;
}
