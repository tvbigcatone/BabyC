#include <stdio.h>
struct S { int a:5; unsigned b:5; };
int main(void) {
  struct S v[4];
  struct S *p;
  int i, t = 0;
  for (i = 0; i < 4; i++) { v[i].a = i; v[i].b = (unsigned)(i + 3); }
  p = v;
  for (i = 0; i < 4; i++) {
    t += p[i].a + (int)p[i].b;
    p[i].a += 1;
  }
  printf("%d\n", t);
  printf("%d %u %d %u\n", v[0].a, v[0].b, v[3].a, v[3].b);
  return 0;
}
