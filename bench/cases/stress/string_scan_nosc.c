#include <stdio.h>
/* Perturbed string_proc: bound check p < end breaks IrTryFastPtrScanPred (*p form). */
int main() {
    char buf[256];
    int i, k, n = 0;
    char *end;
    for (i = 0; i < 255; i++) buf[i] = 'a' + (i % 26);
    buf[255] = 0;
    end = buf + 255;
    for (k = 0; k < 20000; k++) {
        char *p = buf;
        while (p < end) {
            if (*p == 'm' || *p == 'x') n++;
            p++;
        }
    }
    printf("%d\n", n);
    return 0;
}
