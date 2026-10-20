#include <stdio.h>
int len2(const char *s, const char *t) {
    int n = 0;
    while (*s++) n++;
    while (*t++) n++;
    return n;
}
int main() {
    int k = 1;
    printf("%d\n", len2(k ? "hi" : "", k ? "!" : "no"));
    k = 0;
    printf("%d\n", len2(k ? "hi" : "x", k ? "!" : "yz"));
    return 0;
}
