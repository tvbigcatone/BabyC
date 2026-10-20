/* Anti-regression: pointer ternary as direct printf %s arg under Body-IR */
#include <stdio.h>
int main() {
    char *a = "yes";
    char *b = "no";
    printf("%s\n", a ? a : b);
    printf("%s\n", 0 ? a : b);
    printf("%s %s\n", a ? a : b, 0 ? a : b);
    return 0;
}
