#include "printf_format.h"
/* Body-IR-safe printf converters including %n (store char count via pointer). */
int PrintfFormatIsIrSafe(const char *fmt) {
  if (!fmt) return 0;
  for (const char *p = fmt; *p; p++) {
    if (*p == '%') {
      p++;
      if (*p == '%') continue;
      /* flags */
      while (*p=='-'||*p=='+'||*p==' '||*p=='#'||*p=='0'||*p=='\'') p++;
      /* width */
      if (*p=='*') p++; else while (*p>='0'&&*p<='9') p++;
      /* precision */
      if (*p=='.') {
        p++;
        if (*p=='*') p++;
        else while (*p>='0'&&*p<='9') p++;
      }
      /* length */
      if (*p=='h') { p++; if (*p=='h') p++; }
      else if (*p=='l') { p++; if (*p=='l') p++; }
      else if (*p=='L'||*p=='z'||*p=='t'||*p=='j'||*p=='q') p++;
      /* conversion — %n allowed (pointer arg marked escaped after Intrinsic) */
      if (*p=='d'||*p=='i'||*p=='u'||*p=='x'||*p=='X'||*p=='o'
          ||*p=='c'||*p=='s'||*p=='p'||*p=='n'
          ||*p=='f'||*p=='F'||*p=='g'||*p=='G'||*p=='e'||*p=='E'
          ||*p=='a'||*p=='A'||*p=='m')
        continue;
      return 0;
    }
  }
  return 1;
}
