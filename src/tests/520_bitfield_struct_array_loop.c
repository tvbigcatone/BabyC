/* S2: array of struct with bitfields — Body-IR local + loop */
#include <stdio.h>
struct S { int a:5; unsigned b:3; int c:8; };
int main() {
  struct S v[8];
  int i, t;
  for (i = 0; i < 8; i++) {
    v[i].a = i;
    v[i].b = (unsigned)(i & 7);
    v[i].c = i * 3;
  }
  t = 0;
  for (i = 0; i < 8; i++) {
    t += v[i].a + (int)v[i].b + v[i].c;
    v[i].a += 1;
    v[i].b += 1;
  }
  printf("%d\n", t);
  t = 0;
  for (i = 0; i < 8; i++)
    t += v[i].a + (int)v[i].b;
  printf("%d\n", t);
  return 0;
}
