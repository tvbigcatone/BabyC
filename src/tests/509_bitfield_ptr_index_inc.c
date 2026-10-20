#include <stdio.h>
struct T { unsigned idx:3; unsigned flag:1; };
int main() {
  int buf[8];
  struct T t;
  int i, v;
  for (i = 0; i < 8; i++) buf[i] = 100 + i;
  t.idx = 2;
  t.flag = 1;
  v = buf[t.idx++];
  printf("%d\n", v);
  printf("%d\n", t.idx);
  printf("%d\n", buf[t.idx]);
  t.idx = 1;
  v = buf[++t.idx];
  printf("%d\n", v);
  printf("%d\n", t.idx);
  printf("%d\n", t.flag);
  return 0;
}
