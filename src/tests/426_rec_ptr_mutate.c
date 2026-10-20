#include <stdio.h>
void fill(int *p, int n, int v) {
    if (n <= 0) return;
    *p = v;
    fill(p + 1, n - 1, v + 1);
}
int main() {
    int a[4];
    int i;
    fill(&a[0], 4, 1);
    for (i = 0; i < 4; i = i + 1)
        printf("%d%c", a[i], i == 3 ? '\n' : ' ');
    return 0;
}
