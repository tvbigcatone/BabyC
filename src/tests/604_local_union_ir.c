#include <stdio.h>
int main() {
  union U { int i; char c; };
  union U u;
  u.i = 0x41424344;
  printf("%d\n", u.i);
  u.c = 'Z';
  printf("%c\n", u.c);
  return 0;
}
