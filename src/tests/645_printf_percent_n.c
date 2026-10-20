#include <stdio.h>

int main() {
  int n = -1, n2 = -1;
  int x = 0;

  printf("hello%n\n", &n);
  printf("n=%d\n", n);

  printf("ab%ncd%n\n", &n, &n2);
  printf("%d %d\n", n, n2);

  n = -1;
  printf("val=%d%n!\n", 42, &n);
  printf("after=%d\n", n);

  {
    char buf[32];
    n = -1;
    sprintf(buf, "abc%n", &n);
    printf("buf=%s n=%d\n", buf, n);
  }

  n = -1;
  sscanf("99xx", "%d%n", &x, &n);
  printf("x=%d n=%d\n", x, n);

  printf("%hhd %hd\n", (char)-1, (short)-2);
  printf("%hhu %hu\n", (unsigned char)255, (unsigned short)40000);

  return 0;
}
