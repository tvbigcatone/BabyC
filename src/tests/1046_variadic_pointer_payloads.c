#include <stdio.h>
#include <stdarg.h>
struct Item { int value; };
static long long read_values(int count, ...) {
    va_list ap; int *ip; long long wide; struct Item *sp; long long result;
    va_start(ap, count);
    ip = va_arg(ap, int *); wide = va_arg(ap, long long); sp = va_arg(ap, struct Item *);
    va_end(ap);
    result = (long long)*ip + wide + sp->value;
    return result;
}
int main(void) {
    int x = 12; struct Item item; item.value = 9;
    printf("%lld\n", read_values(3, &x, 1234567890123LL, &item));
    return 0;
}
