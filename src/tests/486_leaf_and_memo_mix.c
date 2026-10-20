#include <stdio.h>
int add(int x, int y) { return x + y; }
int fact(int n) {
    if (n <= 1) return 1;
    return n * fact(n - 1);
}
int main() {
    int i, t;
    t = 0;
    for (i = 0; i < 5; i = i + 1)
        t = t + add(i, fact(3));
    printf("%d\n", t);
    return 0;
}
