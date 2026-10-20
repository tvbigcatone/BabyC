/* S2: nested for using BF struct members as bounds */
#include <stdio.h>
struct S { int n:8; int m:8; };
int main() {
  struct S s;
  int i, j, t;
  s.n = 3; s.m = 4;
  t = 0;
  for (i = 0; i < s.n; i++)
    for (j = 0; j < s.m; j++)
      t += i * 10 + j;
  printf("%d\n", t);
  return 0;
}
