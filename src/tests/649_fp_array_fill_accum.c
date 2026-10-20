#include <stdio.h>
int main() {
    int a[100];
    int i, s = 0;
    for (i = 0; i < 100; i++) a[i] = i;
    for (i = 0; i < 100; i++) s += a[i];
    printf("%d\n", s);
    return 0;
}
