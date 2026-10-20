#include <stdio.h>
#include <stdarg.h>
int forward(char *b, const char *f, ...) {
    va_list ap;
    int n;
    va_start(ap, f);
    n = vsnprintf(b, 64, f, ap);
    va_end(ap);
    return n;
}
int main(void) {
    char b[64];
    int n = forward(b, "%d|%.2f|%s|%lld", 7, 2.5, "x", 9000000000LL);
    printf("%d %s\n", n, b);
    return 0;
}
