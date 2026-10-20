#include <stdio.h>
struct T { int bf:4; int pad:4; };
int main() {
  int p[16];
  struct T t;
  int i;
  for (i = 0; i < 16; i++) p[i] = i * 10;
  t.bf = 3;
  printf("%d\n", p[t.bf++]);
  printf("%d\n", t.bf);
  printf("%d\n", p[++t.bf]);
  printf("%d\n", t.bf);
  return 0;
}
