#include <stdio.h>
void main() {
  int x = 5;
  int *px = &x;
  int **ppx = &px;
  int ***pppx = &ppx;
  printf("a=%d\n", - - x);
  printf("b=%d\n", - - - x);
  printf("c=%d\n", -***pppx);
  printf("d=%d\n", - -****&pppx);
}
