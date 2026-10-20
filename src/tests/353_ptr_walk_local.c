/* Anti-regression: char buf[N] + char *p walk in Body IR */
#include <stdio.h>
int main() {
    char buf[8];
    char *p;
    int s = 0;
    buf[0] = 'x'; buf[1] = 'y'; buf[2] = 0;
    p = buf;
    while (*p) { s += *p; p++; }
    printf("%d\n", s);
    return 0;
}
