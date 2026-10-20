#include <stdio.h>
int main(void) {
  int i, s = 0;
  for (i = 0; i < 5; i++)
    s += i;
  printf("%d\n", s);
  return 0;
}
