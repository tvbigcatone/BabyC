/* S2: multiple BF struct locals + call by pointer */
#include <stdio.h>
struct S { int a:6; int b:6; };
void add(struct S *p, int n) { p->a += n; p->b += n; }
int sum(struct S *p) { return p->a + p->b; }
int main() {
  struct S s1, s2;
  s1.a = 10; s1.b = 20;
  s2.a = 1;  s2.b = 2;
  add(&s1, 5);
  add(&s2, 3);
  printf("%d %d\n", sum(&s1), sum(&s2));
  s1.a = s2.a;
  s1.b = s2.b;
  printf("%d %d\n", s1.a, s1.b);
  return 0;
}
