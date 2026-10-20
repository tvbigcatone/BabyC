#include <stdio.h>
int main(void) {
    int i = 0;
    int sum = 0;
    while (i < 20) {
        i++;
        sum += i;
        if (i == 7) break;
    }
    printf("%d %d\n", i, sum);
    return 0;
}
