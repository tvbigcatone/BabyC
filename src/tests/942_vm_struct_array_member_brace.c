#include <stdio.h>
struct S { int arr[3]; int z; };
int main(void) {
  struct S g = {{1, 2, 3}};
  struct S h = {{10, 20, 30}, 99};
  printf("%d %d %d\n", g.arr[0], g.arr[1], g.arr[2]);
  printf("%d %d %d %d\n", h.arr[0], h.arr[1], h.arr[2], h.z);
  return 0;
}
