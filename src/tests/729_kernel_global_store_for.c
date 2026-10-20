/* Const-bound for must persist global scalar stores (KernelFor path). */
#include <stdio.h>
int G = 0;
int H = 7;
int main() {
    int i;
    for (i = 0; i < 10; i++)
        G = i;
    for (i = 0; i < 5; i++)
        H = H + 1;
    printf("%d %d\n", G, H);
    return 0;
}
