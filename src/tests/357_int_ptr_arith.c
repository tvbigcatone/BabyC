/* Anti-regression: int* scaled p+1, a+n, *p */
#include <stdio.h>
int main() {
    int a[5];
    int *p;
    int i, s = 0;
    int diff;
    for (i = 0; i < 5; i = i + 1)
        a[i] = (i + 1) * 10;
    p = a + 1;
    s = *p + *(p + 1);
    diff = p - a;
    printf("%d %d\n", s, diff);
    return 0;
}
