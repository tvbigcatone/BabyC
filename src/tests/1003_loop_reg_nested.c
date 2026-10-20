#include <stdio.h>
int main(void) {
    int i = 0;
    int j = 0;
    while (i < 3) {
        j = 0;
        while (j < 4) { i++; j++; }
        i++;
    }
    printf("%d %d\n", i, j);
    return 0;
}
