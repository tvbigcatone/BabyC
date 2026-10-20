#include <stdio.h>
int main(void) {
    int effects = 0, x = 3;
    if ((x == 3 ? 1 : 0) ? (effects = effects + 1) : (effects = effects + 10)) x = x + 4;
    if ((x == 7 && (effects = effects + 1)) ? 1 : 0) x = x + 8;
    printf("x=%d effects=%d\n", x, effects);
    return 0;
}
