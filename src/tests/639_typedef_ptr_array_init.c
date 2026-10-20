#include <stdio.h>
#include <stdint.h>
int main() {
  int32_t *a[3] = {0};
  int32_t **p = &a[0];
  int32_t x = 7;
  a[1] = &x;
  printf("%d %d %d\n", a[0] == 0, *a[1], p != 0);
  return 0;
}
