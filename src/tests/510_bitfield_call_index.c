#include <stdio.h>
struct T { int bf:5; };
int get(int *p, int i) { return p[i]; }
void set(int *p, int i, int v) { p[i] = v; }
int main() {
  int a[10];
  struct T t;
  int i;
  for (i = 0; i < 10; i++) a[i] = i * i;
  t.bf = 4;
  printf("%d\n", get(a, t.bf++));
  printf("%d\n", t.bf);
  set(a, t.bf, 99);
  printf("%d\n", a[5]);
  printf("%d\n", get(a, ++t.bf));
  printf("%d\n", t.bf);
  return 0;
}
