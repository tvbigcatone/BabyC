#include <stdio.h>
int add2(int a, int b) { return a + b; }
int main(void) {
  int x = add2(3, 4);
  int y = add2(x, add2(1, 2));
  printf("%d %d\n", x, y);
  return 0;
}
