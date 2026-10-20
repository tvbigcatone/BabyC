#include <stdio.h>
int dbl(int x) { return x * 2; }
int sq(int x) { return x * x; }
/* pass fptr via intermediate local (param fptr typedef limited) */
int apply_one(int x) {
    int (*f)(int);
    f = dbl;
    return f(x);
}
int main() {
    int (*fp)(int);
    int (*gp)(int);
    int s;
    fp = dbl;
    gp = sq;
    s = fp(5);
    s = s + gp(3);
    s = s + apply_one(7);
    printf("%d\n", s);
    return 0;
}
