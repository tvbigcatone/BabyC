/* switch + goto in function body IR */
#include <stdio.h>
int classify(int x) {
    switch (x) {
        case 0: return 100;
        case 1: return 101;
        case 2: goto two;
        default: return -1;
    }
two:
    return 102;
}
int main() {
    printf("%d\n", classify(0));
    printf("%d\n", classify(1));
    printf("%d\n", classify(2));
    printf("%d\n", classify(9));
    return 0;
}
