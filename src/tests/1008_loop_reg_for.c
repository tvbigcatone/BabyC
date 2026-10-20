#include <stdio.h>
int main(void) {
    int i = 0;
    int total = 0;
    for (i = 1; i <= 10; i++) {
        if (i == 3) continue;
        total = total + i;
        if (i == 8) break;
    }
    printf("%d %d\n", i, total);
    return 0;
}
