/* Anti-regression: fixed-size array out-of-bounds must fail */
#include <stdio.h>
int main() {
    int a[3];
    a[0] = 1;
    printf("%d\n", a[0]);
    return 0;
}
