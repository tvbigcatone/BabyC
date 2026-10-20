#include <stdio.h>
int main() {
    char buf[64];
    int i, k, n = 0;
    for (i = 0; i < 63; i++) buf[i] = 'a' + (i % 26);
    buf[63] = 0;
    for (k = 0; k < 500; k++) {
        char *p = buf;
        while (*p) {
            if (*p == 'm' || *p == 'x') n++;
            p++;
        }
    }
    printf("%d\n", n);
    return 0;
}
