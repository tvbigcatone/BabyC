#include <stdio.h>
int main() {
    int a[5] = {1,2,3,4,5};
    int *p, s = 0, i;
    for (i = 0; i < 5; i++) {
        p = a;
        while (i < 5 && p < a + 5) { s += *p; p++; break; }
    }
    /* simpler: */
    s = 0;
    for (p = a; p < a + 5; p++) s += *p;
    printf("%d\n", s);
    return 0;
}
