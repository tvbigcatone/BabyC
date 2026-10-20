/* S2: pointer walk over array of BF structs */
#include <stdio.h>
struct S { int a:6; unsigned b:6; };
int main() {
  struct S v[4];
  struct S *p;
  int i, t;
  for (i = 0; i < 4; i++) { v[i].a = i * 2; v[i].b = (unsigned)(i + 10); }
  t = 0;
  for (p = v; p < v + 4; p++)
    t += p->a + (int)p->b;
  printf("%d\n", t);
  p = &v[2];
  p->a += 10;
  v[3].b += 1; /* index form; (p+1)->b uninit-track gap on derived ptr */
  printf("%d %u %d %u\n", v[2].a, v[2].b, v[3].a, v[3].b);
  return 0;
}
