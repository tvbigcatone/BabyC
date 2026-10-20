/* S2: derived-pointer member write via explicit index (safe path) */
#include <stdio.h>
struct S { int a:4; int b:4; };
int main() {
  struct S v[3];
  struct S *p;
  int i;
  for (i = 0; i < 3; i++) { v[i].a = i; v[i].b = i + 1; }
  p = v;
  p[0].a += 5;
  p[1].b += 2;
  p[2].a = 9;
  printf("%d %d %d %d %d %d\n", v[0].a, v[0].b, v[1].a, v[1].b, v[2].a, v[2].b);
  return 0;
}
