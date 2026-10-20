#include <stdio.h>

static int scan_values(const char *input, const char *format, ...);
#include <stdarg.h>
static int scan_values(const char *input, const char *format, ...)
{
    va_list ap;
    int r;
    va_start(ap, format);
    r = vsscanf(input, format, ap);
    va_end(ap);
    return r;
}

int main(void)
{
    int i;
    double d;
    char text[16];
    long long ll;
    int r;
    r = sscanf("12", "%d", &i);
    printf("%d:%d\n", r, i);
    r = sscanf("2.75", "%lf", &d);
    printf("%d:%.2f\n", r, d);
    r = sscanf("word", "%15s", text);
    printf("%d:%s\n", r, text);
    r = scan_values("9000000000", "%lld", &ll);
    printf("%d:%lld\n", r, ll);
    return 0;
}
