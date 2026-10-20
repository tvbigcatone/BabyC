#include <stdio.h>
int main(void) {
    int m[2][3] = {10, 20, 30, 40, 50, 60};
    int *p = &m[0][0];
    int *q = &m[1][2];
    printf("%d\n", p[0]);
    printf("%d\n", p[5]);
    printf("%d\n", *q);
    printf("%d\n", (int)(q - p));
    return 0;
}
