#include <stdio.h>
int f0(int x) { return x + 0; }
int f1(int x) { return x + 1; }
int f2(int x) { return x + 2; }
int main() {
    int (*t0)(int);
    int (*t1)(int);
    int (*t2)(int);
    int (*fp)(int);
    int i, s = 0;
    t0 = f0; t1 = f1; t2 = f2;
    for (i = 0; i < 3; i = i + 1) {
        if (i == 0) fp = t0;
        else if (i == 1) fp = t1;
        else fp = t2;
        s = s + fp(10);
    }
    /* re-assign and call again */
    fp = t1;
    s = s + fp(1);
    printf("%d\n", s);
    return 0;
}
