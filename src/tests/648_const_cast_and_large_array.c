/* Body-IR: (const char*) cast and char buf[8192] must compile and run. */
#include <stdio.h>
#include <string.h>

static const char *as_const(void *p)
{
    return (const char *)p;
}

static int copy_big(const char *src)
{
    char buf[8192];
    int i;
    for (i = 0; i < 8191 && src[i]; i++)
        buf[i] = src[i];
    buf[i] = 0;
    return (int)strlen(buf);
}

int main(void)
{
    char msg[16];
    const char *p;
    int n;
    strcpy(msg, "hello");
    p = as_const(msg);
    n = copy_big(p);
    printf("%s %d\n", p, n);
    return 0;
}
