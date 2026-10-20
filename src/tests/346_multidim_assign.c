/* Anti-regression: multi-dim array assign a[i][j] = v */
#include <stdio.h>
int main() {
    int a[2][3];
    int i, j;
    for (i = 0; i < 2; i++)
        for (j = 0; j < 3; j++)
            a[i][j] = i * 10 + j;
    printf("%d %d %d\n", a[0][0], a[1][2], a[0][2]);
    return 0;
}
