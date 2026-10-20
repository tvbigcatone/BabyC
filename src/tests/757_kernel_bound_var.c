#include <stdio.h>
int main(void) {
  int n = 5;
  int i, s = 0;
  for (i = 0; i < n; i++)
    s += i;
  printf("%d\n", s);
  return 0;
}
