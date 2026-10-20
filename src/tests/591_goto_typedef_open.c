#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef unsigned int u32;
int open_name(char **out, const char *name) {
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
    u32 flags = 3;
    int rc = open_name(&z, "x.db");
    printf("%d %s %u\n", rc, z ? z : "?", flags);
    free(z);
    return 0;
}
