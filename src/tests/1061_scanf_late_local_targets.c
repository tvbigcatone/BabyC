#include <stdio.h>
#include <stdarg.h>

static int scan_many(const char *input, const char *format, ...)
{
    va_list ap;
    int result;
    va_start(ap, format);
    result = vsscanf(input, format, ap);
    va_end(ap);
    return result;
}

int main(void)
{
    char first[4];
    char second[8];
    int i;
    double d;
    long long ll;
    unsigned u;
    int r;
    r = sscanf("12 3.5", "%d %lf", &i, &d);
    printf("%d %d %.1f\n", r, i, d);
    r = scan_many("9000000000 42 word", "%lld %u %7s", &ll, &u, second);
    printf("%d %lld %u %s\n", r, ll, u, second);
    r = sscanf("x", "%3s", first);
    printf("%d %s\n", r, first);
    return 0;
}
