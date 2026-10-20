#include <stdio.h>
#include <stdarg.h>

struct Item { int id; const char *name; };
static struct Item item = {17, "item"};
static int values[2] = {4, 5};

static int probe(const char *fmt, ...)
{
    va_list ap;
    int v;
    int *p;
    va_start(ap, fmt);
    printf("%s\n", va_arg(ap, char *));
    printf("%c\n", *va_arg(ap, const char *));
    printf("%d\n", va_arg(ap, int *)[1]);
    printf("%d\n", *(va_arg(ap, int *) + 1));
    printf("%d\n", va_arg(ap, struct Item *)->id);
    printf("%s\n", (const char *)va_arg(ap, char *));
    p = fmt != 0 ? va_arg(ap, int *) : (int *)0;
    v = p[1];
    printf("%d\n", v);
    va_end(ap);
    return v;
}

int main(void)
{
    probe("take", "hello", "xyz", values, values, &item, "cast-ok", values);
    printf("%d\n", values[1]);
    return 0;
}
