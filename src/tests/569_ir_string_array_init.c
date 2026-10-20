#include <stdio.h>
int main(void) {
  char s[] = "hello";
  char t[8] = "ab";
  printf("%s %c\n", s, s[1]);
  printf("%s %d\n", t, (int)t[2]);
  return 0;
}
