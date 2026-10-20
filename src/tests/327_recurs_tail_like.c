/* Anti-regression: recursive helper with local + param (not pure pattern) */
#include <stdio.h>
int sumto(int n) {
    int s;
    if (n <= 0) return 0;
    s = n;
    return s + sumto(n - 1);
}
int main() {
    printf("%d\n", sumto(100));
    return 0;
}
