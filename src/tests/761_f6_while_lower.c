#include <stdio.h>
int main(void) {
  int i = 0, s = 0;
  while (i < 5) {
    s += i;
    i++;
  }
  printf("%d\n", s);
  return 0;
}
