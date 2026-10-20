/* Cross: for + switch break/continue + goto — C semantics (break leaves only switch) */
#include <stdio.h>
int run(int n) {
    int i, acc = 0;
    for (i = 0; i < n; i++) {
        switch (i) {
            case 0: acc = acc + 1; continue;
            case 1: acc = acc + 10; break;
            case 2: acc = acc + 100; goto tail;
            default: acc = acc + 1000; break;
        }
        acc = acc + 1;
    }
tail:
    return acc;
}
int main(void) {
    printf("%d\n", run(5));
    return 0;
}
