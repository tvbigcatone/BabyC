#include <stdio.h>
int add(int x, int y) { return x + y; }
int main() {
    int i, t;
    t = 0;
    for (i = 0; i < 10; i = i + 1)
        t = t + add(i, 1);
    printf("%d\n", t);
    return 0;
}
