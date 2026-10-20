#include <stdio.h>
int main() {
    int a[3];
    int *p;
    int i;
    for (i = 0; i < 3; i = i + 1) a[i] = i + 1;
    p = &a[3];
    p = p - 1;
    printf("%d\n", *p);
    return 0;
}
