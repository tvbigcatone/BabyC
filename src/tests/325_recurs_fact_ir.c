/* Anti-regression: recursive factorial via Body IR call path */
#include <stdio.h>
int fact(int n) {
    if (n <= 1) return 1;
    return n * fact(n - 1);
}
int main() {
    printf("%d\n", fact(10));
    return 0;
}
