#include <stdio.h>
int a0(int x) { return x; }
int a1(int x) { return x + 1; }
int a2(int x) { return x + 2; }
int main() {
    int (*t0)(int); int (*t1)(int); int (*t2)(int); int (*fp)(int);
    int i, s = 0;
    t0 = a0; t1 = a1; t2 = a2;
    for (i = 0; i < 3; i = i + 1) {
        if (i == 0) fp = t0;
        else if (i == 1) fp = t1;
        else fp = t2;
        s = s + fp(10);
    }
    printf("%d\n", s);
    return 0;
}
