#include <stdio.h>
int main(void) {
  unsigned int w[3]; int i; unsigned s = 0;
  for (i = 0; i < 3; i++) w[i] = 4000000000u + (unsigned)i;
  for (i = 0; i < 3; i++) s += *(w + i) >> 4;
  printf("%u\n", s);
  return 0;
}
