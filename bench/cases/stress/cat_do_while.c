#include <stdio.h>
int sum_do(int n) {
    int s = 0;
    do { s = s + n; n = n - 1; } while (n > 0);
    return s;
}
int run(int k) {
    int i, t = 0;
    for (i = 0; i < k; i = i + 1) t = t + sum_do(100);
    return t;
}
int main() { printf("%d\n", run(300000)); return 0; }
