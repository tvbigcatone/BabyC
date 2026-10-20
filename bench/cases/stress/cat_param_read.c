#include <stdio.h>
static int sink;
int mix(int a, int b, int c) {
    sink ^= a;
    return a * 3 + b * 5 - c;
}
int run(int n) {
    int i, s = 0;
    for (i = 0; i < n; i = i + 1)
        s = s + mix(i, i + 1, i + 2);
    return s;
}
int main() {
    printf("%d %d\n", run(500000), sink);
    return 0;
}
