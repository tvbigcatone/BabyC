#include <stdio.h>
int main() {
  int fails = 0;
  printf(fails ? "FAIL\n" : "ok\n");
  fails = 1;
  printf(fails ? "FAIL\n" : "ok\n");
  return 0;
}
