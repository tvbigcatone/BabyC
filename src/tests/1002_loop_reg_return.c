#include <stdio.h>
static int f(int limit) {
    int i = 0;
    while (i < limit) {
        i++;
        if (i == 5) return i + 10;
    }
    return i;
}
int main(void) { printf("%d %d\n", f(20), f(3)); return 0; }
