#include <stdio.h>
int main() {
    int i, s = 0;
    for (i = 0; i < 100; i++) s += i;
    printf("%d\n", s);
    return 0;
}
