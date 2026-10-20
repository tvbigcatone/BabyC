#include <stdio.h>
#include <stdlib.h>
int main(void) {
  int *p = (int *)malloc(sizeof(int) * 4);
  int i;
  for (i = 0; i < 4; i++) *(p + i) = i + 10;
  for (i = 0; i < 4; i++) printf("%d%c", p[i], i == 3 ? '\n' : ' ');
  free(p);
  return 0;
}
