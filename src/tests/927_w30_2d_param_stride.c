#include <stdio.h>
static int f(int a[][4]) {
    printf("inside %d %d %d\n", a[0][3], a[1][0], a[2][1]);
    return a[0][3] + a[1][0] + a[2][1];
}
static int g(int (*a)[4]) { return a[2][1]; }
int main(void) {
    int a[3][4] = {{1,2,3,4},{10,11,12,13},{20,21,22,23}};
    printf("%d %d %d %d\n", a[0][3], a[1][0], f(a), g(a));
    return 0;
}
