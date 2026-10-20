/* Anti-regression: repeated scan (perf path) */
#include <stdio.h>
int main() {
    char buf[4];
    char *p;
    int i, s = 0;
    buf[0] = 1; buf[1] = 2; buf[2] = 3; buf[3] = 0;
    for (i = 0; i < 1000; i++) {
        p = buf;
        while (*p) { s += *p; p++; }
    }
    printf("%d\n", s);
    return 0;
}
