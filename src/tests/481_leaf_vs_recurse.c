#include <stdio.h>
int add(int x, int y) { return x + y; }
int fact(int n) {
    if (n <= 1) return 1;
    return n * fact(n - 1);
}
int main() {
    printf("%d %d\n", add(2, 3), fact(5));
    return 0;
}
