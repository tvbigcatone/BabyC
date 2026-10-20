/* fprintf with literal format → Body-IR (grey-list) */
#include <stdio.h>
int main() {
  int x = 7;
  fprintf(stdout, "x=%d\n", x);
  fprintf(stderr, "err %s\n", "ok");
  return 0;
}
