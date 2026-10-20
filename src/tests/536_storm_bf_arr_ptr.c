/* storm distill: array of BF structs, pointer walk, compound */
#include <stdio.h>
struct S { int a:6; unsigned b:5; };
int main(void) {
  struct S v[5];
  struct S *p;
  int i, t = 0;
  for (i = 0; i < 5; i++) { v[i].a = i; v[i].b = (unsigned)(i + 1); }
  for (p = v; p < v + 5; p++) {
    t += (int)p->a + (int)p->b;
    p->a += 1;
    p->b += 2;
  }
  for (i = 0; i < 5; i++) t += (int)v[i].a + (int)v[i].b;
  printf("%d\n", t);
  return 0;
}
