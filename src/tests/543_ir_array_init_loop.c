/* S5: Body-IR array init used as loop bounds / accum */
#include <stdio.h>
int sum3(void) {
  int v[3] = { 10, 20, 30 };
  int i, s = 0;
  for (i = 0; i < 3; i++) s += v[i];
  return s;
}
int main(void) {
  int w[] = { 1, 2, 3, 4, 5 };
  int i, s = 0;
  for (i = 0; i < 5; i++) s += w[i];
  printf("%d %d\n", sum3(), s);
  return 0;
}
