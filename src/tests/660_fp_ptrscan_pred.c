#include <stdio.h>
/* Byte-scan with pure predicate: count chars matching == || == */
int main() {
    char buf[64];
    int i, n = 0;
    char *p;
    for (i = 0; i < 63; i++) buf[i] = 'a' + (i % 26);
    buf[63] = 0;
    p = buf;
    while (*p) {
        if (*p == 'a' || *p == 'b') n++;
        p++;
    }
    printf("%d\n", n);
    return 0;
}
