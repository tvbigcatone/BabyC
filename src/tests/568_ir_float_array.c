#include <stdio.h>
int main(void) {
  double a[3] = { 1.0, 2.5, -3.5 };
  a[1] = a[1] * 2.0;
  printf("%f %f %f\n", a[0], a[1], a[2]);
  return 0;
}
