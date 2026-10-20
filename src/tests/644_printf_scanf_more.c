#include <stdio.h>
#include <string.h>

int main() {
  char buf[64];
  int n, x, y;
  char c;
  char s1[16], s2[16];

  /* many args */
  printf("%d %d %d %d %d %d %d %d %d\n",
         1,2,3,4,5,6,7,8,9);

  /* mixed in one format */
  printf("%s %d %c %x\n", "mix", 10, 'A', 0xab);

  /* sprintf return value */
  n = sprintf(buf, "%d+%d=%d", 2, 3, 5);
  printf("n=%d buf=%s\n", n, buf);

  /* sscanf return count */
  n = sscanf("a 1 b 2", "%c %d %c %d", &c, &x, &c, &y);
  printf("n=%d x=%d y=%d\n", n, x, y);

  /* two strings */
  n = sscanf("foo bar", "%15s %15s", s1, s2);
  printf("%d %s %s\n", n, s1, s2);

  /* %% and literal */
  printf("100%% done\n");

  /* flags combination */
  printf("%#08x\n", 0x1f);

  /* snprintf truncation */
  n = snprintf(buf, 5, "%s", "abcdefgh");
  printf("n=%d buf=%s\n", n, buf);

  /* fprintf stderr */
  printf("err\n");  /* stdout: portable suite capture */

  /* format from array */
  {
    char f[8];
    strcpy(f, "%d\n");
    printf(f, 55);
  }

  return 0;
}
