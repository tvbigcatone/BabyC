#include <stdio.h>
int main(void) {
    int i = 0;
    int s = 0;
    do { i++; if (i == 2) continue; s += i; } while (i < 4);
    printf("%d %d\n", i, s);
    return 0;
}
