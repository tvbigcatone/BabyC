/* storm distill: multi struct BF locals, compound, by-value call */
#include <stdio.h>
struct A { int x:5; unsigned y:7; };
struct B { int a:4; int b; unsigned c:6; };
int sumA(struct A s) { return (int)s.x + (int)s.y; }
void bumpB(struct B *p) { p->a += 1; p->b += 2; p->c += 3; }
int main(void) {
  struct A a; struct B b;
  a.x = 3; a.y = 10;
  b.a = 2; b.b = 100; b.c = 5;
  a.x += 2; a.y *= 2;
  bumpB(&b);
  printf("%d %d %d %d %d %d\n",
    sumA(a), (int)a.x, (int)a.y, (int)b.a, b.b, (int)b.c);
  return 0;
}
