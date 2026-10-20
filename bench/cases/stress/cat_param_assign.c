#include <stdio.h>
int step(int acc, int x) {
    acc += x;
    acc ^= (x << 1);
    acc = acc + (x * 3);
    return acc;
}
int run(int n) {
    int i, s = 0;
    for (i = 0; i < n; i = i + 1)
        s = step(s, i);
    return s;
}
int main() {
    printf("%d\n", run(400000));
    return 0;
}
