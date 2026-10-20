#include <stdio.h>
int main(void) {
  float a[4]; int i; float s = 0;
  for (i = 0; i < 4; i++) a[i] = i;
  for (i = 0; i < 4; i++) s += a[i];
  printf("%f\n", s);
  return 0;
}
