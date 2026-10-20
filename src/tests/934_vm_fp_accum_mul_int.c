#include <stdio.h>
int main(void) {
  double a[4]; int i, n = 3; double s = 0;
  for (i = 0; i < 4; i++) a[i] = i * 0.5;
  for (i = 0; i < 4; i++) s += a[i] * n;
  printf("%f\n", s);
  return 0;
}
