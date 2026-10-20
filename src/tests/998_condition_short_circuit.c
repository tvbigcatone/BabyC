#include <stdio.h>
int main(void) {
    int effects = 0, x = 0;
    if (0 && (effects = effects + 1)) x = 10;
    if (1 || (effects = effects + 1)) x = x + 1;
    if ((x == 1 && (effects = effects + 1)) || (0 && (effects = effects + 1))) x = x + 2;
    printf("x=%d effects=%d\n", x, effects);
    return 0;
}
