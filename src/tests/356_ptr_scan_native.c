/* Anti-regression: native while (*p) { s += *p; p++; } */
#include <stdio.h>
int main() {
    char buf[8];
    char *p;
    int s = 0;
    buf[0] = 'A'; buf[1] = 'B'; buf[2] = 'C'; buf[3] = 0;
    p = buf;
    while (*p) { s += *p; p++; }
    printf("%d\n", s);
    return 0;
}
