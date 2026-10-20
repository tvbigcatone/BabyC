#include <stdio.h>
int main(void) {
    int i = -3; unsigned int u = 4U; long l = 2L; unsigned long ul = 3UL;
    printf("%d %d %d %d\n", i < u, l < u, (l + ul) > 0, (i + 8) * 2);
    printf("%d %d %d %d\n", (i ? u : 2U) == 4U, (0, l) == 2L, (int)(u - 2U), (unsigned int)(i + 4));
    return 0;
}
