/* S2: whole struct assign with bitfields */
#include <stdio.h>
struct S { int a:5; int b:3; unsigned c:8; };
int main() {
  struct S s1, s2;
  s1.a = 12; s1.b = 5; s1.c = 200;
  s2 = s1;
  printf("%d %d %u\n", s2.a, s2.b, s2.c);
  s2.a = 1;
  printf("%d %d\n", s1.a, s2.a);
  return 0;
}
