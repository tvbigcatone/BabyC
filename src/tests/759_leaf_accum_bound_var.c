#include <stdio.h>
int add1(int x) { return x + 1; }
int main(void) {
  int n = 4, i, s = 0;
  for (i = 0; i < n; i++)
    s = s + add1(i);
  printf("%d\n", s);
  return 0;
}
