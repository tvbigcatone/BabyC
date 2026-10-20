#include <stdio.h>
int main(void) {
    char text[] = "abcde";
    char *p = text;
    int n = 0;
    while (*p) { n += *p; p++; }
    printf("%d %d\n", n, (int)(p - text));
    return 0;
}
