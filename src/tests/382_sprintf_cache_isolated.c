#include <stdio.h>
#include <string.h>
/* ISOLATED: FormatPure sprintf cache edges
 * - hit same key
 * - miss different key
 * - %s must NOT use int-cache path (still correct)
 * - zero-pad / width still correct when cached
 */
int main()
{
    char a[40], b[40], c[40], d[40];
    sprintf(a, "%04d", 7);
    sprintf(b, "%04d", 7);       /* hit */
    sprintf(c, "%04d", 8);       /* miss */
    sprintf(d, "x%s", "ok");     /* %s — not FormatPure int path */
    printf("%s\n", a);
    printf("%s\n", b);
    printf("%s\n", c);
    printf("%s\n", d);
    return 0;
}
