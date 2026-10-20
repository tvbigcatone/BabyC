/* Anti-regression: char* param scan */
#include <stdio.h>
int sumc(char *p) {
    int s = 0;
    while (*p) { s += *p; p++; }
    return s;
}
int main() {
    printf("%d\n", sumc("xy"));
    return 0;
}
