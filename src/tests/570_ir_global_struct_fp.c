#include <stdio.h>
struct point { double x; double y; };
struct point p;
struct point arr[4];
int main(void) {
  int i = 2;
  p.x = 12.34;
  p.y = 56.78;
  arr[i].x = 1.5;
  arr[i].y = 2.5;
  printf("%f %f\n", p.x, p.y);
  printf("%f %f\n", arr[i].x, arr[i].y);
  return 0;
}
