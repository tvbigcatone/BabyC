#include <stdio.h>
int main(void){
  int a[2][2] = {{1L, (-1L)}, {2, (-2)}};
  int b[3] = {(-1L), -1L, (-1)};
  printf("%d %d %d %d | %d %d %d\n", a[0][0], a[0][1], a[1][0], a[1][1], b[0], b[1], b[2]);
  return 0; }
