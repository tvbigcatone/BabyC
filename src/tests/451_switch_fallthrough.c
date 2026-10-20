#include <stdio.h>
int f(int x) {
    int s;
    s = 0;
    switch (x) {
        case 1:
            s = s + 10;
        case 2:
            s = s + 1;
            break;
        case 3:
            s = 30;
            break;
        default:
            s = -1;
    }
    return s;
}
int main() {
    printf("%d\n", f(1));
    printf("%d\n", f(2));
    printf("%d\n", f(3));
    printf("%d\n", f(0));
    return 0;
}
