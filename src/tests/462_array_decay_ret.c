#include <stdio.h>

int buf[5];

int *ret_buf(void) { return buf; }
int *ret_offset(int i) { return buf + i; }

void fill(int *a, int n, int base) {
    int i;
    for (i = 0; i < n; i = i + 1)
        a[i] = base + i;
}

int main() {
    int *p;
    int i;
    fill(ret_buf(), 5, 10);
    for (i = 0; i < 5; i = i + 1) {
        if (i) printf(" ");
        printf("%d", buf[i]);
    }
    printf("\n");
    p = ret_offset(2);
    *p = 99;
    p[1] = 88;
    printf("%d %d\n", buf[2], buf[3]);
    ret_buf()[0] = 1;
    ret_buf()[4] = 5;
    printf("%d %d\n", buf[0], buf[4]);
    return 0;
}
