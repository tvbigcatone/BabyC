#include <stdio.h>
void fill(int *a, int n) {
    int i;
    for (i = 0; i < n; i++) a[i] = i + 1;
}
int main() {
    int buf[8];
    int i, s = 0;
    fill(buf, 8);
    for (i = 0; i < 8; i++) s += buf[i];
    printf("%d\n", s);
    return 0;
}
