/* while path control for global scalar stores */
#include <stdio.h>
int G = 0;
int main() {
    int i = 0;
    while (i < 10) {
        G = i;
        i = i + 1;
    }
    printf("%d\n", G);
    return 0;
}
