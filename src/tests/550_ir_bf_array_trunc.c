#include <stdio.h>
struct S { int n:4; };
int main(void) {
  struct S a[2];
  a[0].n = 0;
  a[1].n = 7;
  a[0].n += 1;
  a[1].n += 1;
  printf("%d %d\n", a[0].n, a[1].n);
  return 0;
}
