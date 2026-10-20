#include <stdio.h>
#include <string.h>

/* Rich format coverage for Body-IR printf/scanf greylist. */

int main() {
  char buf[128];
  char s[32];
  int a, b, c;
  long la;
  long long lla;
  unsigned ua;
  unsigned long long ull;
  double d;
  void *p;
  const char *fmt;

  /* --- integers --- */
  printf("%d %i %u\n", -3, 4, 5u);
  printf("%x %X %#x %o\n", 255, 255, 255, 8);
  printf("%ld %lu\n", 100L, 200UL);
  printf("%lld %llu\n", 1LL, 2ULL);
  printf("%+d % 5d %-6d %05d\n", 7, 8, 9, 10);

  /* --- char / string --- */
  printf("%c %s\n", 'Z', "hello");
  printf("%-10s %.3s\n", "abcdef", "abcdef");
  printf("%*.*s\n", 8, 4, "wxyz");

  /* --- pointer / float --- */
  p = (void *)0;
  /* portable: avoid glibc "(nil)" vs MSVC "00000000" for %p NULL */
  printf("%s\n", p == 0 ? "null" : "non-null");
  d = 3.5;
  printf("%.1f %g %e\n", d, d, d);

  /* --- variable format string --- */
  fmt = "%d %s\n";
  printf(fmt, 42, "varfmt");
  fmt = "%x\n";
  printf(fmt, 0x2a);

  /* --- ternary format --- */
  printf(1 ? "%d\n" : "%s\n", 99);
  printf(0 ? "%d\n" : "%s\n", "no");

  /* --- sprintf / snprintf --- */
  sprintf(buf, "%d:%s", 1, "two");
  printf("%s\n", buf);
  snprintf(buf, sizeof(buf), "%04d", 12);
  printf("%s\n", buf);

  /* --- sscanf multi --- */
  a = b = c = 0;
  sscanf("10 20 30", "%d %d %d", &a, &b, &c);
  printf("%d %d %d\n", a, b, c);

  memset(s, 0, sizeof(s));
  sscanf("word", "%31s", s);
  printf("%s\n", s);

  la = 0;
  lla = 0;
  ua = 0;
  ull = 0;
  sscanf("100 200 300 400", "%ld %lld %u %llu", &la, &lla, &ua, &ull);
  printf("%ld %lld %u %llu\n", la, lla, ua, ull);

  /* hex / octal scan */
  a = 0;
  sscanf("ff", "%x", &a);
  printf("%d\n", a);
  a = 0;
  sscanf("10", "%o", &a);
  printf("%d\n", a);

  /* width */
  a = 0;
  sscanf("12345", "%3d", &a);
  printf("%d\n", a);

  /* variable scanf format */
  fmt = "%d";
  a = 0;
  sscanf("77", fmt, &a);
  printf("%d\n", a);

  /* fprintf */
  fprintf(stdout, "ok\n");

  return 0;
}
