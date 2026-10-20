#include <stdio.h>
int op0(int x) { return x - 1; }
int op1(int x) { return x + 1; }
int op2(int x) { return x * 2; }
int op3(int x) { return x / 2; }
int main() {
    int (*f0)(int); int (*f1)(int); int (*f2)(int); int (*f3)(int);
    int (*fp)(int);
    int i, s;
    f0 = op0; f1 = op1; f2 = op2; f3 = op3;
    s = 0;
    for (i = 0; i < 20; i = i + 1) {
        if ((i & 3) == 0) fp = f0;
        else if ((i & 3) == 1) fp = f1;
        else if ((i & 3) == 2) fp = f2;
        else fp = f3;
        s = s + fp(i + 2);
    }
    printf("%d\n", s);
    return 0;
}
