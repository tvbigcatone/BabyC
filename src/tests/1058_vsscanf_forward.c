#include <stdio.h>
#include <stdarg.h>
int scan(const char *s, const char *f, ...) {
    va_list ap;
    int n;
    va_start(ap, f);
    n = vsscanf(s, f, ap);
    va_end(ap);
    return n;
}
int main(void) {
    int i = 0;
    double d = 0.0;
    char word[8] = {0};
    int n = scan("7 2.5 x", "%d %lf %7s", &i, &d, word);
    printf("%d %d %.1f %s\n", n, i, d, word);
    return 0;
}
