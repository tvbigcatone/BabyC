#include <stdio.h>
int mix(int n, int *cnt) {
  if (n <= 1) { *cnt = *cnt + 1; return n; }
  return mix(n-1, cnt) + mix(n-2, cnt);
}
int main() {
  int cnt = 0;
  int v = mix(4, &cnt);
  printf("%d %d\n", v, cnt);
  return 0;
}
