/* Anti-regression: switch inside for (Irb / Body IR shared IrStSwitch) */
#include <stdio.h>
int main() {
    int i, s = 0;
    for (i = 0; i < 4; i++) {
        switch (i) {
            case 0: s = s + 1; break;
            case 1: s = s + 10; break;
            case 2: s = s + 100; break;
            default: s = s + 1000; break;
        }
    }
    printf("%d\n", s);
    return 0;
}
