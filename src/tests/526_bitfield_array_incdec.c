/* S2: ++/-- on bitfield members of array elements */
#include <stdio.h>
struct S { int a:4; unsigned b:4; };
int main() {
  struct S v[3];
  int i;
  for (i = 0; i < 3; i++) { v[i].a = i; v[i].b = (unsigned)(i + 1); }
  printf("%d %u\n", v[0].a++, v[1].b++);
  printf("%d %u\n", ++v[0].a, ++v[1].b);
  printf("%d %u\n", v[2].a--, v[2].b--);
  printf("%d %u %d %u\n", v[0].a, v[0].b, v[1].a, v[1].b);
  printf("%d %u\n", v[2].a, v[2].b);
  return 0;
}
