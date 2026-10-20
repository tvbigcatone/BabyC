#include <stdio.h>
struct Mix { char *s; int *ip; };
int main() {
    char buf[8];
    int n;
    struct Mix m;
    buf[0] = 'A'; buf[1] = 'B'; buf[2] = 0;
    n = 42;
    m.s = &buf[0];
    m.ip = &n;
    printf("%c %d\n", *m.s, *m.ip);
    m.s = m.s + 1;
    printf("%c\n", *m.s);
    return 0;
}
