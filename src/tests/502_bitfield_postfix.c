#include <stdio.h>
struct S { int a:4; int b:5; };
int main() {
  struct S s;
  s.a = 3; s.b = 7;
  printf("%d\n", s.a++);
  printf("%d\n", s.a);
  printf("%d\n", ++s.b);
  printf("%d\n", s.b);
  return 0;
}
