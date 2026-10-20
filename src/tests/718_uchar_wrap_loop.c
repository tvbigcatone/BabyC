#include <stdio.h>
int main(void) {
  unsigned char c = 1;
  int i;
  for (i = 0; i < 10; i++) c = c * 3 + 1;
  printf("%u\n", (unsigned)c);
  c = 0;
  for (i = 0; i < 10; i++) c += 200;
  printf("%u\n", (unsigned)c);
  return 0;
}
