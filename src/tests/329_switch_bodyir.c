/* Anti-regression: switch/case/default in function Body IR */
#include <stdio.h>
int f(int x) {
    switch (x) {
        case 1: return 11;
        case 2: return 22;
        default: return 0;
    }
}
int main() {
    printf("%d %d %d\n", f(1), f(2), f(9));
    return 0;
}
