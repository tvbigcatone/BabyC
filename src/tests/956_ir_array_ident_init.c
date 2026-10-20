#include <stdio.h>
int main(void) {
  int src[3] = {10, 20, 30};
  int dst[] = src;
  printf("%d %d %d %d\n", dst[0], dst[1], dst[2], (int)sizeof(dst));
  return 0;
}
