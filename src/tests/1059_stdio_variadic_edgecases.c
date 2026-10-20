#include <stdio.h>
#include <stdarg.h>

static int fmt(char *b, int n, const char *f, ...)
{
    va_list ap;
    int r;
    va_start(ap, f);
    r = vsnprintf(b, n, f, ap);
    va_end(ap);
    return r;
}

static int vf(const char *f, ...)
{
    va_list ap;
    int r;
    va_start(ap, f);
    r = vprintf(f, ap);
    va_end(ap);
    return r;
}

static int scan(const char *s, const char *f, ...)
{
    va_list ap;
    int r;
    va_start(ap, f);
    r = vsscanf(s, f, ap);
    va_end(ap);
    return r;
}

int main(void)
{
    char b[64];
    int n = -1;
    int i;
    double d;
    char word[16];
    long long ll;
    int r;
    r = fmt(b, sizeof(b), "%*d|%-*d|%.*s", 6, 42, 4, 7, 2, "hello");
    printf("%d [%s]\n", r, b);
    printf("[%*d][%.*f]\n", -4, 7, -1, 2.5);
    r = fmt(b, sizeof(b), "abc%n:%d", &n, 9);
    printf("%d %d [%s]\n", r, n, b);
    r = sscanf("12 3.25 word 9000000000", "%d %lf %15s %lld", &i, &d, word, &ll);
    printf("%d %d %.2f %s %lld\n", r, i, d, word, ll);
    r = scan("13 4.5 test 8000000000", "%d %lf %15s %lld", &i, &d, word, &ll);
    printf("%d %d %.1f %s %lld\n", r, i, d, word, ll);
    vf("vprintf:%d:%.1f\n", 5, 2.5);
    return 0;
}
