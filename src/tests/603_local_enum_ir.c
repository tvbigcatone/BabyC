#include <stdio.h>
int main() {
  enum Color { RED = 1, GREEN = 2, BLUE = 4 };
  enum Color c = GREEN;
  printf("%d\n", c);
  printf("%d\n", RED + BLUE);
  if (1) {
    enum Status { OK = 0, ERR = -1 };
    printf("%d %d\n", OK, ERR);
  }
  return 0;
}
