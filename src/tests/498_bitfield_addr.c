#include <stdio.h>
struct S { int a:3; };
int main() {
  struct S s;
  int *p = &s.a;
  (void)p;
  return 0;
}
