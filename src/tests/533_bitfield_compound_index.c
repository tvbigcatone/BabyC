/* S2: compound assign on indexed bitfield member (was full-word read bug) */
#include <stdio.h>
struct S { int a:4; int b:4; };
int main() {
  struct S v[4];
  int i;
  for (i = 0; i < 4; i++) { v[i].a = i; v[i].b = i + 1; }
  for (i = 0; i < 4; i++) {
    v[i].a += 2;
    v[i].b *= 2;
  }
  for (i = 0; i < 4; i++)
    printf("%d %d\n", v[i].a, v[i].b);
  return 0;
}
