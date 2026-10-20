#include <stdio.h>
struct S { int a:3; };
int main() {
  struct S s;
  printf("%d\n", (int)sizeof(s.a));
  return 0;
}
