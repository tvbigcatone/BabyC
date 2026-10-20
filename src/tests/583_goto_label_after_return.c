/* Anti-regression: goto targets after return (sqlite-style error labels) */
#include <stdio.h>
int classify(int x) {
    if (x < 0) goto neg;
    if (x == 0) goto zero;
    return 1;
neg:
    return -1;
zero:
    return 0;
}
int main() {
    printf("%d %d %d\n", classify(-3), classify(0), classify(9));
    return 0;
}
