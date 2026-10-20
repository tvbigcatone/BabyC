#include <stdio.h>
#include <string.h>
int main() {
    char a[16], b[16];
    memset(a, 0, 16);
    memcpy(a, "hello", 6);
    memmove(b, a, 6);
    printf("%s %s\n", a, b);
    return 0;
}
