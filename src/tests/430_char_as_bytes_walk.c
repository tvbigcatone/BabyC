#include <stdio.h>
int main() {
    int x;
    char *b;
    int s;
    x = 0x01020304;
    b = (char *)&x;
    s = (unsigned char)b[0] + (unsigned char)b[1];
    printf("%d\n", s > 0 ? 1 : 0);
    return 0;
}
