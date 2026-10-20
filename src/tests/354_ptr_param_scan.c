/* Anti-regression: char* param *p / p++ fast path */
#include <stdio.h>
int scan(char *p) {
    int s = 0;
    while (*p) { s += *p; p++; }
    return s;
}
int main() {
    printf("%d\n", scan("ab"));
    return 0;
}
