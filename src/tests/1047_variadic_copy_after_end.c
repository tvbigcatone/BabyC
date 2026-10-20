#include <stdio.h>
#include <stdarg.h>
static int copy_after_end(int count, ...) {
    va_list original, copy; int result;
    va_start(original, count);
    va_copy(copy, original);
    va_end(original);
    result = va_arg(copy, int);
    va_end(copy);
    return result;
}
int main(void) {
    printf("%d\n", copy_after_end(1, 73));
    return 0;
}
