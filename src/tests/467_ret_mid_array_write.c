#include <stdio.h>

int a[6];

int *mid(void) { return &a[2]; }
int *from(int i) { return a + i; }

int main() {
    int i;
    for (i = 0; i < 6; i = i + 1) a[i] = i;
    mid()[0] = 100;
    mid()[1] = 101;
    from(0)[0] = 200;
    from(4)[0] = 204;
    from(4)[1] = 205;
    for (i = 0; i < 6; i = i + 1) {
        if (i) printf(" ");
        printf("%d", a[i]);
    }
    printf("\n");
    return 0;
}
