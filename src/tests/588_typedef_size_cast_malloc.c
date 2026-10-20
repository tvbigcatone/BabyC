#include <stdio.h>
#include <stdlib.h>
typedef unsigned long size_t_like;
int main() {
    int n = 4;
    char *z;
    size_t_like sz = (size_t_like)n + 1;
    z = (char *)malloc((unsigned)sz);
    if (z == 0) { printf("fail\n"); return 1; }
    z[0] = 'A'; z[1] = 0;
    printf("%s %lu\n", z, (unsigned long)sz);
    free(z);
    return 0;
}
