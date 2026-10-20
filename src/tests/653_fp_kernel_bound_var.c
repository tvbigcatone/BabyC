#include <stdio.h>
int sum_to(int n) {
    int i, s = 0;
    for (i = 0; i < n; i++) s += i;
    return s;
}
int main() {
    printf("%d\n", sum_to(50));
    return 0;
}
