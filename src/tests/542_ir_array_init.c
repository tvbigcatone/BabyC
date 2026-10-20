/* S5: Body-IR local array brace-init (fixed size + inferred + partial) */
#include <stdio.h>
int main(void) {
  int a[3] = { 12, 34, 56 };
  int b[] = { 1, 2, 3, 4 };
  int c[5] = { 9, 8 };
  int i, t = 0;
  for (i = 0; i < 3; i++) t += a[i];
  for (i = 0; i < 4; i++) t += b[i];
  for (i = 0; i < 5; i++) t += c[i];
  printf("%d %d %d\n", a[0], a[1], a[2]);
  printf("%d %d %d %d\n", b[0], b[1], b[2], b[3]);
  printf("%d %d %d %d %d\n", c[0], c[1], c[2], c[3], c[4]);
  printf("%d\n", t);
  return 0;
}
