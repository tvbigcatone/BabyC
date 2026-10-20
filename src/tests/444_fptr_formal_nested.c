/* pass fptr through two levels */
#include <stdio.h>
int dbl(int x) { return x * 2; }
int call1(int (*fp)(int), int x) { return fp(x); }
int call2(int (*fp)(int), int x) { return call1(fp, x); }
int main() {
    printf("%d\n", call2(dbl, 6));
    return 0;
}
