#include <stdio.h>
#define MAX_DIGITS 32
int main() {
  char buf[5*MAX_DIGITS];
  int a[MAX_DIGITS+1];
  int i;
  buf[0] = 'A'; buf[1] = 0;
  a[0] = 7;
  a[MAX_DIGITS] = 9;
  printf("%s %d %d\n", buf, a[0], a[MAX_DIGITS]);
  return 0;
}
