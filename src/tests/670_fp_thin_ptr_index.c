/* T* formal used only as a[i] load — thin when pointee scalar */
#include <stdio.h>
static int sum3(int *a, int n) {
    int i, s;
    s = 0;
    for (i = 0; i < n; i++)
        s = s + a[i];
    return s;
}
int main(void) {
    int b[4];
    b[0] = 1; b[1] = 2; b[2] = 3; b[3] = 4;
    printf("%d\n", sum3(b, 4));
    return 0;
}
