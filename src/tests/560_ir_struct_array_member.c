#include <stdio.h>
struct S { int arr[3]; int z; };
int main(void) {
  struct S g = {{1, 2, 3}};
  struct S h = {{10, 20, 30}, 99};
  int i, t = 0;
  for (i = 0; i < 3; i++) t += g.arr[i];
  h.arr[1] += 5;
  printf("%d\n", t);
  printf("%d %d %d %d\n", h.arr[0], h.arr[1], h.arr[2], h.z);
  return 0;
}
