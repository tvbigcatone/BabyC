/* Anti-regression: goto/label in function Body IR */
#include <stdio.h>
int main() {
    int n = 0;
    goto skip;
    n = 1;
skip:
    n = n + 2;
    printf("%d\n", n);
    return 0;
}
