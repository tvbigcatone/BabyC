#include <stdio.h>
int fact(int n) {
    if (n <= 1) return 1;
    return n * fact(n - 1);
}
int add(int a, int b) {
    return a + b;
}
int main() {
    printf("%d\n", fact(5));
    printf("%d\n", add(10, 20));
    return 0;
}
