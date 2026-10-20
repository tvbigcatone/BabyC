#include <stdio.h>
int main() {
    char buf[256];
    int i, k, n = 0;
    for (i = 0; i < 255; i++) buf[i] = 'a' + (i % 26);
    buf[255] = 0;
    for (k = 0; k < 20000; k++) {
        char *p = buf;
        while (*p) {
            if (*p == 'm' || *p == 'x') n++;
            p++;
        }
    }
    printf("%d\n", n);
    return 0;
}
