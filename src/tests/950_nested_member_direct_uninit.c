#include <stdio.h>
struct Inner { int a; int b; };
struct Outer { struct Inner in; int id; };
int main(void) {
  struct Outer v[2];
  int i = 1;
  printf("%d\n", v[i].in.a);
  return 0;
}
