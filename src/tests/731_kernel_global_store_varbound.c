/* Variable-bound for (non-kernel) global store */
#include <stdio.h>
int G = 0;
int main() {
    int i;
    int n = 10;
    for (i = 0; i < n; i++)
        G = i;
    printf("%d\n", G);
    return 0;
}
