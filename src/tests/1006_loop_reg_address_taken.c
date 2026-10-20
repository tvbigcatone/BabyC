#include <stdio.h>
int main(void) {
    int i = 0;
    int *q = &i;
    while (i < 4) { i++; *q += 0; }
    printf("%d\n", *q);
    return 0;
}
