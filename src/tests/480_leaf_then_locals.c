#include <stdio.h>
int add(int x, int y) { return x + y; }
int work(int n) {
    int i, s;
    s = 0;
    for (i = 0; i < n; i = i + 1)
        s = s + add(i, 1);
    return s;
}
int main() {
    printf("%d\n", work(5));
    printf("%d\n", add(100, work(3)));
    return 0;
}
