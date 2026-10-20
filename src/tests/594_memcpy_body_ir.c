/* Anti-regression: memcpy/memmove allowed in Body-IR (was hard-refuse) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int copy_s(char **out, const char *name) {
    char *z;
    int n = 0;
    const char *p;
    if (name == 0) goto error;
    for (p = name; *p; p++) n++;
    if (n <= 0) goto error;
    z = (char *)malloc((unsigned)n + 1);
    if (z == 0) goto error;
    memcpy(z, name, (unsigned)n + 1);
    *out = z;
    return 0;
error:
    return -1;
}
int main() {
    char *z = 0;
    char buf[8];
    int rc = copy_s(&z, "ab");
    /* memmove only — memset stays hard (SEGV diagnostics 389) */
    memmove(buf, "xxx", 4);
    printf("%d %s %s\n", rc, z, buf);
    free(z);
    return 0;
}
