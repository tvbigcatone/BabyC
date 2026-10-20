#include <stdio.h>
int add2(int a, int b) { return a + b; }
int main() {
    int i, s = 0;
    for (i = 0; i < 50; i++) s += add2(3, 4);
    printf("%d\n", s);
    return 0;
}
