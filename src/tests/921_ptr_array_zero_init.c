#include <stdio.h>
int main(void) {
  int *a[3] = {0};
  int x = 7;
  a[1] = &x;
  printf("%d %d %d\n", a[0] == 0, *a[1], a[2] == 0);
  return 0;
}
