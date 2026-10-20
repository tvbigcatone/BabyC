#include <stdio.h>
#include <stdarg.h>
int forward(const char *f, ...) {
    va_list ap;
    int n;
    va_start(ap, f);
    n = vprintf(f, ap);
    va_end(ap);
    return n;
}
int forward_file(FILE *out, const char *f, ...) {
    va_list ap;
    int n;
    va_start(ap, f);
    n = vfprintf(out, f, ap);
    va_end(ap);
    return n;
}
int main(void) {
    int n = forward("%d|%.2f|%s|%lld", 7, 2.5, "x", 9000000000LL);
    printf(" %d", n);
    forward_file(stdout, "|%s", "ok");
    putchar('\n');
    return 0;
}
