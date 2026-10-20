#include <stdio.h>
struct Inner { int a; int b; int c; };
struct Outer { struct Inner x; int y; };
int main(void) {
  int i, t = 0;
  for (i = 0; i < 3; i++) {
    struct Outer o = {{i, i + 1, i + 2}, i + 10};
    t += o.x.a + o.x.b + o.x.c + o.y;
  }
  printf("%d\n", t);
  return 0;
}
