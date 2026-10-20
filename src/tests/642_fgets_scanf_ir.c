#include <stdio.h>
#include <string.h>

/* fgets into local/global buffer; scanf %d into local via & */
static char gbuf[64];

int main() {
  char lbuf[64];
  int n = 0;
  int x = 0;
  /* sscanf is the portable stand-in for scanf (no stdin dependency in suite) */
  n = sscanf("42 hello", "%d", &x);
  printf("%d %d\n", n, x);
  strcpy(lbuf, "line1\n");
  strcpy(gbuf, "line2\n");
  fputs(lbuf, stdout);
  fputs(gbuf, stdout);
  /* Portable empty-file fgets: write empty temp, read → EOF (no /dev/null) */
  {
    FILE *f = fopen("babyc_654_empty.tmp", "wb");
    if (f) {
      fclose(f);
      f = fopen("babyc_654_empty.tmp", "rb");
      if (f) {
        char t[8];
        if (fgets(t, 8, f) == 0)
          printf("eof\n");
        fclose(f);
      }
      remove("babyc_654_empty.tmp");
    }
  }
  return 0;
}
