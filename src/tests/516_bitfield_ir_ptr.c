/* Body-IR: &local_struct, pointer param, by-value param */
#include <stdio.h>
struct S { int a:4; int b:4; unsigned c:8; };
int get_a(struct S s) { return s.a; }
int get_b(struct S *p) { return p->b; }
void set_c(struct S *p, unsigned v) { p->c = v; }
int main() {
  struct S s;
  s.a = 3; s.b = 5; s.c = 10;
  printf("%d %d %u\n", get_a(s), get_b(&s), s.c);
  set_c(&s, 200);
  printf("%u %d\n", s.c, get_b(&s));
  return 0;
}
