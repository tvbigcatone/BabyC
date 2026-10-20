#include <stdio.h>
char *skip_to_zero(char *p)
{
  while (*p++ != 0)
    ;
  return p;
}
int main() {
  char buf[8] = "abc";
  char *p = skip_to_zero(buf);
  printf("%d\n", (int)(p - buf));
  return 0;
}
