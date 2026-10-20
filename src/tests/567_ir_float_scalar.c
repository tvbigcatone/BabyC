#include <stdio.h>
int main(void) {
  double x = 3.14;
  float y = 2.5f;
  x = x + 1.0;
  y = y * 2.0f;
  printf("%f %f\n", x, (double)y);
  return 0;
}
