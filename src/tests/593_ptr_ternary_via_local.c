#include <stdio.h>
int main() {
    char *a = "yes";
    char *b = "no";
    char *s = a ? a : b;
    printf("%s\n", s);
    s = (0 ? a : b);
    printf("%s\n", s);
    return 0;
}
